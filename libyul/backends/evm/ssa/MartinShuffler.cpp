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

#include <libyul/backends/evm/ssa/MartinShuffler.h>

#include "libsolutil/Visitor.h"

#include <ranges>

using namespace solidity::yul::ssa;

namespace
{
class Excess
{
public:
	explicit Excess(std::map<StackSlot, int32_t> excesses) : excesses(std::move(excesses)) {}
	auto begin() const { return excesses.cbegin(); }
	auto end() const { return excesses.cend(); }
	[[nodiscard]] bool hasExcess(StackSlot const& slot) const
	{
		auto const it = excesses.find(slot);
		return it != excesses.end() && it->second > 0;
	}
	[[nodiscard]] bool hasNegativeExcess(StackSlot const& slot) const
	{
		auto const it = excesses.find(slot);
		return it != excesses.end() && it->second < 0;
	}
	[[nodiscard]] int32_t getExcess(StackSlot const& slot) const
	{
		auto const it = excesses.find(slot);
		return it == excesses.end() ? 0 : it->second;
	}
	void increment(StackSlot const& slot)
	{
		++excesses[slot];
	}
	void decrement(StackSlot const& slot)
	{
		--excesses[slot];
	}
private:
	std::map<StackSlot, int32_t> excesses;
};

using OccurrenceCounts = util::UseCountSet<StackSlot>;

Excess computeExcess(StackData const& _source, detail2::Target _target)
{
	std::map<StackSlot, int32_t> excesses;
	for (auto const& slot : _source)
		++excesses[slot];
	for (auto const& slot : _target.args)
		--excesses[slot];
	for (const auto& slot: _target.liveOut | std::views::keys)
		--excesses[slot];
	return Excess{std::move(excesses)};
}

OccurrenceCounts computeCounts(StackData const& _source)
{
	OccurrenceCounts counts;
	for (auto const& slot : _source)
		counts.insert(slot);
	return counts;
}


// bool constexpr canBeFreelyGenerated(StackSlot const& _slot)
// {
// 	return _slot.isLiteralValue() || _slot.isJunk() || _slot.isFunctionCallReturnLabel();
// }

using DupOp = detail2::DupOp;
using SwapOp = detail2::SwapOp;
using PopOp = detail2::PopOp;
using PushOp = detail2::PushOp;

class Shuffler
{
public:
	Shuffler(StackData const& _source, detail2::Target _target) :
		current(_source),
		target(_target),
		excess(computeExcess(_source, _target)),
		counts(computeCounts(_source)),
		executedOps()
	{}
	StackShufflerResult run();
	auto extractOps() && { return std::move(executedOps); }

private:
	StackShufflerResult step();
	StackShufflerResult shrink();
	StackShufflerResult addMissingSlots();
	StackShufflerResult swapToCompletion();

	StackData fixTarget() const;
	void permuteTo(StackData);
	std::size_t missingSlotsCount() const;
	std::optional<StackDepth> slotWithExcess() const;

	/// index scheme conversion offset -> depth
	StackDepth offsetToDepth(StackOffset const& _offset) const
	{
		yulAssert(_offset < current.size(), "Offset out of range");
		return StackDepth{current.size() - _offset.value - 1};
	}
	/// index scheme conversion depth -> offset
	StackOffset depthToOffset(StackDepth const& _depth) const
	{
		yulAssert(_depth < current.size(), "Depth out of range");
		return StackOffset{current.size() - _depth.value - 1};
	}

	void execute(SwapOp);
	void execute(DupOp);
	void execute(PushOp);
	void execute(PopOp);

	StackData current;
	detail2::Target target;
	Excess excess;
	OccurrenceCounts counts;
	std::vector<detail2::StackOp> executedOps;
};

auto extractOps(Shuffler&& shuffler) { return std::move(shuffler).extractOps(); }

std::size_t Shuffler::missingSlotsCount() const
{
	std::size_t missing = 0;
	for (const auto& slotExcess: excess | std::views::values)
	{
		if (slotExcess < 0)
			missing += static_cast<std::size_t>(-slotExcess);
	}
	return missing;
}

StackShufflerResult Shuffler::run()
{
	std::size_t constexpr maxIterations = 1000;
	std::size_t i = 0;
	while (i < maxIterations)
	{
		auto res = step();
		if (res.status == StackShufflerResult::Status::Admissible)
			return res;
		if (res.status == StackShufflerResult::Status::StackTooDeep)
			return res;
		yulAssert(res.status == StackShufflerResult::Status::Continue);
		++i;
	}
	return {StackShufflerResult::Status::MaxIterationsReached};
}

StackShufflerResult Shuffler::step()
{
	if (current.size() > target.targetStackSize)
		return shrink();
	if (missingSlotsCount() > target.targetStackSize - current.size())
		return shrink();
	// Invariant 1: current size <= target size
	if (current.size() < target.targetStackSize)
		return addMissingSlots();
	// Invariant 2
	yulAssert(current.size() == target.targetStackSize);
	// At this point there should not be any negative excess. Positive excess can happen with don't care slots in target
	for (auto const& [slot, slotExcess] : excess)
		yulAssert(slot.isJunk() || slotExcess >= 0);
	return swapToCompletion();
}

StackShufflerResult Shuffler::shrink()
{
	std::optional<StackDepth> maybeDepth = slotWithExcess();
	if (!maybeDepth)
		return {StackShufflerResult::Status::StackTooDeep}; // TODO: We need spilling candidate
	auto const depth = *maybeDepth;
	if (depth != StackDepth{0})
		execute(SwapOp(depth));
	execute(PopOp{});
	return {StackShufflerResult::Status::Continue};
}

StackShufflerResult Shuffler::addMissingSlots()
{
	std::size_t const initialSize = current.size();
	yulAssert(initialSize <= target.targetStackSize);
	if (initialSize == target.targetStackSize)
		return {StackShufflerResult::Status::Continue};

	yulAssert(target.args.size() <= target.targetStackSize);
	std::size_t const argsStart = target.targetStackSize - target.args.size();

	std::map<StackSlot, std::size_t> reservedCounts;
	{
		auto missingArgsBeginIt = target.args.begin();
		if (initialSize > argsStart)
			std::advance(missingArgsBeginIt, initialSize - argsStart);
		for (auto it = missingArgsBeginIt; it != target.args.end(); ++it)
			reservedCounts[*it]++;
	}

	auto const candidates = [this]() -> std::set<StackSlot>
	{
		std::set<StackSlot> candidates;
		auto considerCandidate = [&](StackSlot const& slot)
		{
			if (excess.hasNegativeExcess(slot))
				candidates.insert(slot);
		};
		for (auto const& slot : target.liveOut | std::views::keys)
			considerCandidate(slot);
		for (auto const& slot : target.args)
			considerCandidate(slot);
		return candidates;
	}();

	auto addOnTop = [this](StackSlot const& slot)
	{
		auto findExisting = [&](StackSlot const& slot) -> std::optional<std::size_t>
		{
			for (std::size_t k = 0; k < current.size(); ++k)
				if (current[k] == slot)
					return k;
			return std::nullopt;
		};
		if (auto const existing = findExisting(slot))
			execute(DupOp{offsetToDepth(StackOffset{*existing})});
		else
		{
			yulAssert(canBeFreelyGenerated(slot));
			execute(PushOp{slot});
		}
	};

	while (current.size() < target.targetStackSize)
	{
		std::size_t const offset = current.size();
		if (offset >= argsStart)
		{
			StackSlot const& wanted = target.args[offset - argsStart];
			if (excess.hasNegativeExcess(wanted))
			{
				addOnTop(wanted);
				continue;
			}
		}

		auto next = std::find_if(candidates.begin(), candidates.end(), [&](StackSlot const& slot) {
			auto const reserved = reservedCounts[slot];
			return (static_cast<std::size_t>(-excess.getExcess(slot))) > reserved;
		});
		if (next != candidates.end())
			addOnTop(*next);
		else
			execute(PushOp{StackSlot::makeJunk()});
	}

	return {StackShufflerResult::Status::Continue};
}

StackShufflerResult Shuffler::swapToCompletion()
{
	permuteTo(fixTarget());
	return {StackShufflerResult::Status::Admissible};
}

// Fixes positions for tail slots. JUNK marks don't-care slots
StackData Shuffler::fixTarget() const
{
	yulAssert(current.size() == target.targetStackSize);
	yulAssert(target.targetStackSize >= target.args.size());
	std::size_t const tailSize = target.targetStackSize - target.args.size();
	if (tailSize == 0)
		return target.args;
	StackData fixedTarget(current.size(), StackSlot::makeJunk());
	using diff_t = std::iter_difference_t<decltype(fixedTarget.begin())>;
	// Fix non-arbitrary target args
	std::ranges::copy(target.args, fixedTarget.begin() + static_cast<diff_t>(tailSize));
	// Fix positions for tail slots
	{
		// Fast and Greedy heuristic: If a slot needed in a tail is already in tail, fix it to its current position
		util::UseCountSet<StackSlot> assigned;
		std::vector<std::size_t> tailPositionsToFix;
		tailPositionsToFix.reserve(tailSize);
		for (auto i = 0u; i < tailSize; ++i)
		{
			auto const& currentSlot = current[i];
			if (target.liveOut.contains(currentSlot) && !assigned.contains(currentSlot))
			{
				fixedTarget[i] = currentSlot;
				assigned.insert(currentSlot);
			}
			else
				tailPositionsToFix.push_back(i);
		}
		// Greedy approach: Assign any free tail position to yet-unassigned target tail slots
		for (auto const& slot: target.liveOut | std::views::keys)
		{
			if (assigned.contains(slot))
				continue;
			yulAssert(!tailPositionsToFix.empty());
			fixedTarget[tailPositionsToFix.back()] = slot;
			tailPositionsToFix.pop_back();
		}
	}
	return fixedTarget;
}

void Shuffler::permuteTo(StackData _target)
{
	auto const size = current.size();
	yulAssert(size == _target.size());
	// Phase 1: Compute exact mapping from source indices to target indices
	auto indexAssignment = [&]() -> std::vector<std::size_t>
	{
		std::vector<std::size_t> assignment(size, static_cast<std::size_t>(-1));
		std::vector<char> assigned(size, 0);
		std::vector<char> targetTaken(size, 0);
	    auto claim = [&](std::size_t const i, std::size_t const j)
	    {
		    assignment[i] = j;
		    assigned[i] = true;
		    targetTaken[j] = true;
	    };

		// Fix positions with equal elements in source and target
		// Compute slot to indices map for remaining slots
		std::map<StackSlot, std::vector<std::size_t>> indicesByValue;
		for (auto i = 0u; i < size; ++i)
			if (current[i] == _target[i])
				claim(i,i);
			else
				indicesByValue[current[i]].push_back(i);
		// Assign any JUNK slots in source to JUNK slots in target
		auto constexpr junkSlot = StackSlot::makeJunk();
		if (auto const it = indicesByValue.find(junkSlot); it != indicesByValue.end())
		{
			std::size_t targetIndex = 0;
			for (auto const junkSourceIndex : it->second)
			{
				while (targetIndex < size && !_target[targetIndex].isJunk())
					++targetIndex;
				yulAssert(targetIndex < size);
				claim(junkSourceIndex, targetIndex);
				++targetIndex;
			}
			indicesByValue.erase(it);
		}
		// Match concrete values in source and target
		for (auto i = 0u; i < size; ++i)
		{
			if (targetTaken[i] || _target[i].isJunk())
				continue;
			auto it = indicesByValue.find(_target[i]);
			yulAssert(it != indicesByValue.end());
			yulAssert(!it->second.empty());
			// TODO: Be smart in case there is a choice?
			auto const sourceIndex = it->second.back();
			claim(sourceIndex, i);
			it->second.pop_back();
		}
		// Match any remaining JUNK target slots to whatever is left in the source
		for (auto i = 0u; i < size; ++i)
		{
			if (targetTaken[i])
				continue;
			yulAssert(_target[i].isJunk());
			if (!assigned[i])
			{
				claim(i,i);
				continue;
			}
			// take any remaining source slot
			auto it = std::ranges::find(assigned, 0);
			yulAssert(it != assigned.end());
			claim(static_cast<std::size_t>(it - assigned.begin()), i);
		}
		return assignment;
	}();
	yulAssert(indexAssignment.size() == size);
	yulAssert(std::ranges::all_of(indexAssignment, [size](std::size_t const index){ return index < size; }));
	// Phase 2: Shuffle according to the index assignment
	{
		yulAssert(std::ranges::is_permutation(indexAssignment, std::ranges::views::iota(static_cast<std::size_t>(0), size)));
		auto destinations = indexAssignment;
		yulAssert(size > 0);
		std::size_t const topIndex = size - 1;
		std::size_t currentIndex = topIndex;
		auto executeSwap = [&](std::size_t const index)
		{
			std::swap(destinations[topIndex], destinations[index]);
			execute(SwapOp{offsetToDepth(StackOffset{index})});
		};
		while (currentIndex > 0)
		{
			if (destinations[currentIndex] == currentIndex)
			{
				--currentIndex;
				continue;
			}
			if (currentIndex != topIndex)
				executeSwap(currentIndex);
			while (destinations[topIndex] != topIndex)
				executeSwap(destinations[topIndex]);
			--currentIndex;
		}
	}
	yulAssert(std::ranges::all_of(std::ranges::views::iota(static_cast<std::size_t>(0), size), [&](std::size_t const i)
	{
		return _target[i].isJunk() || _target[i] == current[i];
	}));
}

std::optional<StackDepth> Shuffler::slotWithExcess() const
{
	auto currentDepth = StackDepth{0};
	for (auto const& slot : current | std::views::reverse)
	{
		if (excess.hasExcess(slot))
			return currentDepth;
		currentDepth.value++;
	}
	return std::nullopt;
}

void Shuffler::execute(SwapOp _op)
{
	yulAssert(_op.depth.value != 0);
	StackOffset const offset = depthToOffset(_op.depth);
	StackOffset const topOffset = depthToOffset(StackDepth{0});
	std::swap(current[topOffset.value], current[offset.value]);
	executedOps.emplace_back(_op);
}

void Shuffler::execute(DupOp _op)
{
	StackOffset const offset = depthToOffset(_op.depth);
	StackSlot const& slotToDup = current[offset.value];
	excess.increment(slotToDup);
	counts.insert(slotToDup);
	current.push_back(slotToDup);
	executedOps.emplace_back(_op);
}

void Shuffler::execute(PopOp _op)
{
	yulAssert(!current.empty());
	StackSlot const& slotToPop = current.back();
	excess.decrement(slotToPop);
	counts.remove(slotToPop);
	current.pop_back();
	executedOps.emplace_back(_op);
}

void Shuffler::execute(PushOp _op)
{
	excess.increment(_op.slot);
	counts.insert(_op.slot);
	current.push_back(_op.slot);
	executedOps.emplace_back(_op);
}

} // namespace

detail2::Result detail2::shuffle(StackData const& _source, Target _target)
{
	yulAssert(_source.size() <= detail2::reachableStackDepth && _target.targetStackSize <= detail2::reachableStackDepth);
	Shuffler shuffler(_source, _target);
	auto const res = shuffler.run();
	return Result{.result = res, .ops = extractOps(std::move(shuffler))};
}

[[nodiscard]] StackShufflerResult solidity::yul::ssa::martinShuffle(
	StackData& _stack,
	StackData const& _args,
	StackSlotLiveness const& _liveOut,
	std::size_t const _targetStackSize,
	spill::SpillSet const* _spilledVariables
)
{
	yulAssert(!_spilledVariables || _spilledVariables->numSpilled() == 0, "TODO");
	auto const implResult = detail2::shuffle(_stack, detail2::Target{.args = _args, .liveOut = _liveOut, .targetStackSize = _targetStackSize});
	yulAssert(implResult.result.status == StackShufflerResult::Status::Admissible, "TODO");
	StackShufflerResult result = implResult.result;
	Stack stack(_stack);
	for (auto const& op : implResult.ops)
	{
		std::visit(solidity::util::GenericVisitor
			{
				[&](PushOp const& pushOp){ stack.push(pushOp.slot); result.trace.push_back(ShuffleOp::push(pushOp.slot)); },
				[&](PopOp const&){ stack.pop(); result.trace.pop_back(); },
				[&](DupOp const& dupOp){ stack.dup(dupOp.depth); result.trace.push_back(ShuffleOp::dup(StackDepth{dupOp.depth.value + 1})); },
				[&](SwapOp const& swapOp){ stack.swap(swapOp.depth), result.trace.push_back(ShuffleOp::swap(swapOp.depth)); }
			}, op);
	}
	return result;
}
