/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Property tests for the SSA CFG stack shuffler.
 *
 * A raw fuzz input (slot kind/id sequences plus an id modulus and tail padding) is projected onto a
 * shuffler input that satisfies StackShuffler's preconditions, mirroring the projection of the
 * protobuf-based shuffler fuzzer in solidity-fuzzing (tools/ossfuzz/protoToShuffler.cpp). The id
 * modulus is itself fuzzed: a small modulus yields collision-heavy stacks (duplicate/surplus logic),
 * a large one yields all-distinct deep stacks (spill pressure).
 *
 * Properties:
 *   - ShuffleWithoutSpillingIsSound: shuffle() without a spill set terminates in Admissible or an
 *     honest StackTooDeep with an actionable (spillable, non-literal) culprit — never
 *     MaxIterationsReached. On success, an independent replay of the emitted SWAP/DUP/PUSH/POP
 *     stream reproduces the shuffler's final stack, and that stack is admissible per an
 *     independently coded spec.
 *   - FeasibleTargetsAreReachableWithSpilling: whenever the feasibility conditions hold (every
 *     target slot is on the initial stack, spilled to memory, or freely generatable — guaranteed by
 *     the projection), shuffleWithSpillDiscovery() must reach the target: Admissible,
 *     unconditionally. Same replay and admissibility oracles, spill-aware (pushes of spilled values
 *     are reloads; spilled values are exempt from the live-out retention requirement but target arg
 *     positions must still match exactly).
 */

#include <libyul/backends/evm/ssa/InstructionStore.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>
#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackShuffler.h>
#include <libyul/backends/evm/ssa/StackSlotLiveness.h>
#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libsolutil/Numeric.h>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace solidity;
using namespace solidity::yul::ssa;

namespace solidity::yul::test
{

namespace
{

std::uint8_t constexpr kindVariable = 0;
std::uint8_t constexpr kindPhi = 1;
std::uint8_t constexpr kindLiteral = 2;
std::uint8_t constexpr kindJunk = 3;

/// Raw fuzz-domain slot; projected onto a StackSlot with the id clamped by the input's modulus.
struct RawSlot
{
	std::uint8_t kind;
	std::uint32_t id;
};

struct RawShuffleInput
{
	std::vector<RawSlot> initial;
	std::vector<RawSlot> targetTop;
	std::vector<RawSlot> tailSet;
	/// Values pre-spilled to memory ("in memory" per the feasibility conditions); they need not be
	/// on the initial stack. Only used by the spilling property.
	std::vector<RawSlot> spillSeeds;
	/// Slot ids are taken modulo this. Small values force slot collisions (duplicates on stack),
	/// large values yield pairwise-distinct stacks that exert spill pressure.
	std::uint32_t idModulus;
	std::uint32_t extraTailPadding;
};

std::size_t constexpr maxInitialSize = 48;
std::size_t constexpr maxTargetTopSize = 24;
std::size_t constexpr maxTargetSize = 48;
std::size_t constexpr maxSpillSeeds = 8;
std::uint32_t constexpr maxSlotUniverse = 256;

static_assert(maxTargetTopSize <= maxTargetSize);

/// Shuffler input satisfying the preconditions of the shuffle variant it was projected for.
struct ProjectedInput
{
	StackData initial;
	StackData targetTop;
	StackSlotLiveness targetTail{};
	std::size_t targetStackSize = 0;
	spill::SpillSet initialSpills{};
	/// Every distinct value slot minted during projection, for rendering and kind lookups. The
	/// backing InstructionStore is not kept: StackSlots are self-contained (they cache the opcode).
	std::map<InstId, StackSlot> mintedValues{};
};

/// Projects a raw input onto a precondition-satisfying shuffler input, following the same rules as
/// the protobuf fuzzer's convertProtoInput. With `_spillingAllowed` the target feasibility widens to
/// the full condition "on stack, in memory (spilled), or freely generatable"; without it, target
/// slots must be physically on the initial stack (or JUNK), matching the fuzzer's conservative
/// projection.
ProjectedInput project(RawShuffleInput const& _raw, bool const _spillingAllowed)
{
	InstructionStore store;
	std::map<std::pair<std::uint8_t, std::uint32_t>, StackSlot> memo;
	ProjectedInput out;

	// Memoize (kind, clamped id) -> StackSlot so equal raw slots collapse onto the same value;
	// each appendXxx() mints a fresh InstId, so without this every slot would be unique.
	auto const mint = [&](RawSlot const& _slot) -> StackSlot
	{
		if (_slot.kind >= kindJunk)
			return StackSlot::makeJunk();
		std::uint32_t const id = _slot.id % _raw.idModulus;
		auto const key = std::pair{_slot.kind, id};
		if (auto const it = memo.find(key); it != memo.end())
			return it->second;
		InstId const instId =
			_slot.kind == kindPhi ? store.appendPhi({0}) :
			_slot.kind == kindLiteral ? store.appendLiteral({0}, u256(id)).first :
			store.appendBuiltinCall({0}, {}, {});
		StackSlot const slot = StackSlot::makeValue(store, instId);
		memo.emplace(key, slot);
		out.mintedValues.emplace(instId, slot);
		return slot;
	};

	out.initial.reserve(_raw.initial.size());
	for (auto const& raw: _raw.initial)
		out.initial.push_back(mint(raw));

	std::set<StackSlot> onInitial;
	for (auto const& slot: out.initial)
		if (slot.isValue())
			onInitial.insert(slot);

	if (_spillingAllowed)
		for (auto const& raw: _raw.spillSeeds)
			if (StackSlot const slot = mint(raw);
				slot.isValue() && !slot.isLiteralValue() && !out.initialSpills.isSpilled(slot.value()))
				out.initialSpills.add(slot.value());

	// Feasibility for target top slots. All minted slots are values or junk.
	auto const feasibleTopSlot = [&](StackSlot const& _slot)
	{
		if (_slot.isJunk() || onInitial.contains(_slot))
			return true;
		return _spillingAllowed &&
			(Stack<>::canBeFreelyGenerated(_slot) || out.initialSpills.isSpilled(_slot.value()));
	};

	out.targetTop.reserve(std::min(_raw.targetTop.size(), maxTargetTopSize));
	for (auto const& raw: _raw.targetTop)
	{
		if (out.targetTop.size() == maxTargetTopSize)
			break;
		StackSlot slot = mint(raw);
		if (!feasibleTopSlot(slot))
			slot = StackSlot::makeJunk();
		out.targetTop.push_back(slot);
	}

	std::size_t const tailRoomCap = maxTargetSize - out.targetTop.size();

	// Tail (live-out) set: deduplicated non-literal values that are on the initial stack or, with
	// spilling, in memory. Liveness is a property of non-constant values, so literals and junk are
	// dropped rather than junked.
	StackSlotLiveness::Entries liveEntries;
	{
		std::set<StackSlot> seen;
		for (auto const& raw: _raw.tailSet)
		{
			if (liveEntries.size() == tailRoomCap)
				break;
			StackSlot const slot = mint(raw);
			if (!slot.isValue() || slot.isLiteralValue())
				continue;
			bool const feasible = onInitial.contains(slot) ||
				(_spillingAllowed && out.initialSpills.isSpilled(slot.value()));
			if (!feasible || !seen.insert(slot).second)
				continue;
			liveEntries.emplace_back(slot, 1u);
		}
	}
	out.targetTail = StackSlotLiveness{std::move(liveEntries)};

	std::size_t const minTail = out.targetTail.size();
	std::size_t const extraPadding = _raw.extraTailPadding % (tailRoomCap - minTail + 1);
	out.targetStackSize = out.targetTop.size() + minTail + extraPadding;
	return out;
}

/// Renders a projected input in the .stack file format of test/libyul/ssa/stackShuffler, so a
/// failing case can be replayed with isoltest after translating the printed slot names.
std::string describe(ProjectedInput const& _input)
{
	std::ostringstream out;
	out << "initial: " << stackToString(_input.initial) << "\n";
	out << "targetStackTop: " << stackToString(_input.targetTop) << "\n";
	out << "targetStackTailSet: {";
	bool first = true;
	for (auto const& [slot, count]: _input.targetTail)
	{
		out << (first ? "" : ", ") << slotToString(slot);
		first = false;
	}
	out << "}\n";
	out << "targetStackSize: " << _input.targetStackSize << "\n";
	out << "initialSpilledSet: {";
	first = true;
	for (InstId const value: _input.initialSpills.spilledValues())
	{
		out << (first ? "" : ", ") << slotToString(_input.mintedValues.at(value));
		first = false;
	}
	out << "}\n";
	return out.str();
}

/// Structured record of a single shuffler-emitted stack operation. Kept typed (rather than
/// formatted and re-parsed) so the replay oracle is free of format-level ambiguity.
struct Op
{
	enum class Kind { Swap, Dup, Push, Pop };
	Kind kind;
	std::size_t depth = 0; // Swap/Dup only
	StackSlot slot{};      // Push only
};

struct RecordingCallbacks
{
	std::vector<Op>* ops;

	void swap(StackDepth const _depth) { ops->push_back({Op::Kind::Swap, _depth.value, {}}); }
	void dup(StackDepth const _depth) { ops->push_back({Op::Kind::Dup, _depth.value, {}}); }
	void push(StackSlot const& _slot) { ops->push_back({Op::Kind::Push, 0, _slot}); }
	void pop() { ops->push_back({Op::Kind::Pop, 0, {}}); }
};
static_assert(StackManipulationCallbackConcept<RecordingCallbacks>);

/// Replays the emitted operations on `_stack`, validating each against the EVM reachability rules
/// (SWAP1..16, DUP1..16) and `_pushAllowed` for PUSH. Any violation means the shuffler emitted an
/// opcode the EVM (or the spill machinery) could not honor.
void replayOps(
	StackData& _stack,
	std::vector<Op> const& _ops,
	std::function<bool(StackSlot const&)> const& _pushAllowed
)
{
	std::size_t constexpr reachable = 16;
	for (auto const& op: _ops)
		switch (op.kind)
		{
		case Op::Kind::Swap:
			ASSERT_GE(op.depth, 1u) << "shuffler emitted SWAP out of range";
			ASSERT_LE(op.depth, reachable) << "shuffler emitted SWAP out of range";
			ASSERT_GT(_stack.size(), op.depth) << "shuffler emitted SWAP deeper than stack";
			std::swap(_stack[_stack.size() - 1 - op.depth], _stack.back());
			break;
		case Op::Kind::Dup:
			ASSERT_GE(op.depth, 1u) << "shuffler emitted DUP out of range";
			ASSERT_LE(op.depth, reachable) << "shuffler emitted DUP out of range";
			ASSERT_GE(_stack.size(), op.depth) << "shuffler emitted DUP deeper than stack";
			_stack.push_back(_stack[_stack.size() - op.depth]);
			break;
		case Op::Kind::Push:
			ASSERT_TRUE(_pushAllowed(op.slot))
				<< "shuffler PUSHed a slot that is neither freely generatable nor a spill reload: "
				<< slotToString(op.slot);
			_stack.push_back(op.slot);
			break;
		case Op::Kind::Pop:
			ASSERT_FALSE(_stack.empty()) << "shuffler emitted POP on empty stack";
			_stack.pop_back();
			break;
		}
}

/// Independent admissibility spec, coded against the target semantics rather than reusing
/// State::admissible(): the final stack has target size, every non-JUNK target arg sits exactly at
/// its position, and every value demanded by the target (per non-JUNK, non-spilled arg occurrence
/// plus one per non-spilled live-out entry) has enough copies somewhere on the stack. Spilled
/// values live in memory, so they are exempt from the count requirement — but not from exact arg
/// placement, since arg slots are reloaded.
testing::AssertionResult independentlyAdmissible(
	StackData const& _final,
	ProjectedInput const& _input,
	spill::SpillSet const* const _spills
)
{
	if (_final.size() != _input.targetStackSize)
		return testing::AssertionFailure()
			<< "final stack size " << _final.size() << " != target size " << _input.targetStackSize;

	auto const spilled = [&](StackSlot const& _slot)
	{
		return _spills && _slot.isValue() && _spills->isSpilled(_slot.value());
	};

	std::size_t const tailSize = _input.targetStackSize - _input.targetTop.size();
	for (std::size_t i = 0; i < _input.targetTop.size(); ++i)
	{
		StackSlot const& expected = _input.targetTop[i];
		if (!expected.isJunk() && _final[tailSize + i] != expected)
			return testing::AssertionFailure()
				<< "target arg " << slotToString(expected) << " missing at offset " << tailSize + i
				<< ", found " << slotToString(_final[tailSize + i]);
	}

	std::map<StackSlot, std::size_t> required;
	for (auto const& arg: _input.targetTop)
		if (!arg.isJunk() && !spilled(arg))
			++required[arg];
	for (auto const& [slot, count]: _input.targetTail)
		if (!spilled(slot))
			++required[slot];
	for (auto const& [slot, needed]: required)
		if (static_cast<std::size_t>(std::count(_final.begin(), _final.end(), slot)) < needed)
			return testing::AssertionFailure()
				<< "stack holds fewer than " << needed << " copies of " << slotToString(slot);

	return testing::AssertionSuccess();
}

auto rawSlotDomain()
{
	return fuzztest::StructOf<RawSlot>(
		fuzztest::InRange<std::uint8_t>(0, kindJunk),
		fuzztest::InRange<std::uint32_t>(0, maxSlotUniverse - 1)
	);
}

auto rawShuffleInputDomain()
{
	return fuzztest::StructOf<RawShuffleInput>(
		fuzztest::VectorOf(rawSlotDomain()).WithMaxSize(maxInitialSize),
		fuzztest::VectorOf(rawSlotDomain()).WithMaxSize(maxTargetTopSize),
		fuzztest::VectorOf(rawSlotDomain()).WithMaxSize(maxTargetSize),
		fuzztest::VectorOf(rawSlotDomain()).WithMaxSize(maxSpillSeeds),
		fuzztest::InRange<std::uint32_t>(1, maxSlotUniverse),
		fuzztest::InRange<std::uint32_t>(0, static_cast<std::uint32_t>(maxTargetSize))
	);
}

std::vector<RawSlot> variables(std::uint32_t const _firstId, std::size_t const _count)
{
	std::vector<RawSlot> result;
	result.reserve(_count);
	for (std::size_t i = 0; i < _count; ++i)
		result.push_back({kindVariable, _firstId + static_cast<std::uint32_t>(i)});
	return result;
}

/// Deterministic regression shapes for the spilling property.
std::vector<std::tuple<RawShuffleInput>> spillingSeeds()
{
	// A tall stack of pairwise-distinct values whose bottom 20 values are all demanded as target
	// args: unreachable without spilling, so spill discovery carries the whole load.
	RawShuffleInput deepDistinct{
		.initial = variables(0, 40),
		.targetTop = variables(0, 20),
		.tailSet = variables(20, 10),
		.spillSeeds = {},
		.idModulus = maxSlotUniverse,
		.extraTailPadding = 0,
	};

	// The spilled-value reload thrash shape: v0 is already spilled AND required in args (so its
	// on-stack copy reads as surplus but keeps being reloaded), while v1 has a genuine deficit
	// (needed twice, present once) that keeps triggering shrinking.
	RawShuffleInput reloadThrash{
		.initial = variables(0, 20),
		.targetTop = {{kindVariable, 0}, {kindVariable, 1}, {kindVariable, 1}},
		.tailSet = variables(2, 12),
		.spillSeeds = {{kindVariable, 0}},
		.idModulus = maxSlotUniverse,
		.extraTailPadding = 2,
	};

	return {{std::move(deepDistinct)}, {std::move(reloadThrash)}};
}

} // anonymous namespace

/// Without a spill set the shuffler may honestly give up (StackTooDeep with an actionable culprit),
/// but it must never livelock, and on success the emitted opcode stream must independently
/// reproduce an admissible stack.
void ShuffleWithoutSpillingIsSound(RawShuffleInput const& _raw)
{
	ProjectedInput const input = project(_raw, false);

	std::vector<Op> ops;
	StackData stackData = input.initial;
	Stack<RecordingCallbacks> stack(stackData, RecordingCallbacks{&ops});
	StackShufflerResult const result = StackShuffler<RecordingCallbacks>::shuffle(
		stack,
		input.targetTop,
		input.targetTail,
		input.targetStackSize
	);

	ASSERT_NE(result.status, StackShufflerResult::Status::Continue);
	ASSERT_NE(result.status, StackShufflerResult::Status::MaxIterationsReached)
		<< "shuffler livelocked without spilling on:\n" << describe(input);

	if (result.status == StackShufflerResult::Status::StackTooDeep)
	{
		// The reported culprit must be actionable, i.e. spilling it must be possible.
		ASSERT_TRUE(result.spillingCandidate.isValue() && !result.spillingCandidate.isLiteralValue())
			<< "unactionable stack-too-deep culprit " << slotToString(result.spillingCandidate)
			<< " on:\n" << describe(input);
		return;
	}

	ASSERT_EQ(result.status, StackShufflerResult::Status::Admissible);
	StackData replayed = input.initial;
	replayOps(replayed, ops, [](StackSlot const& _slot) { return Stack<>::canBeFreelyGenerated(_slot); });
	ASSERT_EQ(replayed, stackData)
		<< "replay of emitted opcodes diverges from the shuffler's stack on:\n" << describe(input);
	ASSERT_TRUE(independentlyAdmissible(stackData, input, nullptr)) << "on:\n" << describe(input);
}

FUZZ_TEST(StackShufflerProperty, ShuffleWithoutSpillingIsSound)
	.WithDomains(rawShuffleInputDomain());

/// The central completeness property: the projection guarantees the feasibility conditions (every
/// target slot is on the initial stack, spilled to memory, or freely generatable), so with spill
/// discovery the shuffler must always reach the target — no give-up state, no livelock.
void FeasibleTargetsAreReachableWithSpilling(RawShuffleInput const& _raw)
{
	ProjectedInput const input = project(_raw, true);

	spill::SpillSet spills = input.initialSpills;
	std::vector<Op> ops;
	StackData stackData = input.initial;
	Stack<RecordingCallbacks> stack(stackData, RecordingCallbacks{&ops});
	StackShufflerResult const result = StackShuffler<RecordingCallbacks>::shuffleWithSpillDiscovery(
		stack,
		input.targetTop,
		input.targetTail,
		input.targetStackSize,
		spills
	);

	ASSERT_EQ(result.status, StackShufflerResult::Status::Admissible)
		<< "shuffler failed to reach a feasible target with spilling allowed (status "
		<< static_cast<int>(result.status) << ") on:\n" << describe(input);

	// The spill set only grows, and everything in it is a spillable (non-literal) value.
	for (InstId const value: input.initialSpills.spilledValues())
		ASSERT_TRUE(spills.isSpilled(value));
	for (InstId const value: spills.spilledValues())
	{
		auto const it = input.mintedValues.find(value);
		ASSERT_NE(it, input.mintedValues.end()) << "shuffler spilled a value that was never minted";
		ASSERT_FALSE(it->second.isLiteralValue()) << "shuffler spilled a literal";
	}

	// Replay oracle; a PUSH of a spilled value is a reload from memory.
	StackData replayed = input.initial;
	replayOps(
		replayed,
		ops,
		[&](StackSlot const& _slot)
		{
			return Stack<>::canBeFreelyGenerated(_slot) || (_slot.isValue() && spills.isSpilled(_slot.value()));
		}
	);
	ASSERT_EQ(replayed, stackData)
		<< "replay of emitted opcodes diverges from the shuffler's stack on:\n" << describe(input);

	ASSERT_TRUE(independentlyAdmissible(stackData, input, &spills)) << "on:\n" << describe(input);
}

FUZZ_TEST(StackShufflerProperty, FeasibleTargetsAreReachableWithSpilling)
	.WithDomains(rawShuffleInputDomain())
	.WithSeeds(spillingSeeds());

}
