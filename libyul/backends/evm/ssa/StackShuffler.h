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

#pragma once

#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackSlotLiveness.h>
#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <boost/container/flat_map.hpp>

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>

#include <cstddef>
#include <optional>

namespace solidity::yul::ssa
{

namespace detail
{

inline bool slotIsSpilled(StackSlot const& _slot, spill::SpillSet const* const _slots)
{
	return _slots && _slot.isValue() && _slots->isSpilled(_slot.value());
}

/// True if `_slot` can be materialized at a structural leaf (PUSH for literals/junk/return
/// labels, MLOAD for spilled values). The shuffler's heuristics MUST NOT consult this
/// predicate — its sole legitimate use is at points where the algorithm has otherwise
/// exhausted its options for producing `_slot` from the current stack (e.g. the arg is not
/// on stack and not dup-reachable). Routing this predicate into any priority/expendability
/// path makes the trace dependent on spill-set membership and violates the invariance
/// contract documented on `StackShuffler::shuffle`.
inline bool slotCanBeLoadedOrPushed(StackSlot const& _slot, spill::SpillSet const* const _slots)
{
	return Stack<>::canBeFreelyGenerated(_slot) || slotIsSpilled(_slot, _slots);
}

/// Contains information about the shuffling target, aggregates over args and live out to
/// provide a lower bound for the slot distribution.
struct Target
{
	Target(
		StackData const& _args,
		StackSlotLiveness const& _liveOut,
		std::size_t _targetSize,
		spill::SpillSet const* _slots = nullptr
	);

	StackData const& args;
	StackSlotLiveness const& liveOut;
	spill::SpillSet const* const slots;
	std::size_t const size;
	std::size_t const tailSize;
	boost::container::flat_map<StackSlot, size_t> minCount;
};
/// Current state of the stack vs the shuffling target.
class State
{
public:
	State(StackData const& _stackData, Target const& _target, spill::SpillSet const* _slots, std::size_t _reachableStackDepth);

	std::size_t size() const;
	/// How many of `_slot` do we have on stack
	std::size_t count(StackSlot const& _slot) const;
	/// How many of `_slot` do we have in the args section of the stack
	std::size_t countInArgs(StackSlot const& _slot) const;
	/// How many of `_slot` do we have in the tail section of the stack
	std::size_t countInTail(StackSlot const& _slot) const;
	/// How many of `_slot` are (dup) reachable on stack
	std::size_t countReachable(StackSlot const& _slot) const;

	/// Obtain the amount of the provided slot that is required for distribution correctness
	std::size_t targetMinCount(StackSlot const& _slot) const;
	/// Obtain the amount of the provided slot that is required in target args
	std::size_t targetArgsCount(StackSlot const& _slot) const;

	bool willRequireShrinking() const;

	/// Checks if the state is compatible with the target
	bool admissible() const;

	/// Checks if a particular slot is required in the target args
	bool requiredInArgs(StackSlot const& _slot) const;
	/// Checks if a particular slot is required in the target tail
	bool requiredInTail(StackSlot const& _slot) const;

	/// Checks if an offset is in the target args (bounded from below by tail size, from above by target size)
	bool offsetInTargetArgsRegion(StackOffset _offset) const;
	/// Retrieves the required argument slot for a specific stack offset
	StackSlot const& targetArg(StackOffset _targetOffset) const;
	/// Checks the current stack offset is args-compatible with a target stack offset, meaning the target offset is
	/// in the target args region and either a wildcard slot (JUNK) or a precise match for the slot at `_sourceOffset`
	bool isArgsCompatible(StackOffset _sourceOffset, StackOffset _targetOffset) const;
	/// Checks if the slot at `_targetOffset` admits any slot
	bool targetArbitrary(StackOffset _targetOffset) const;
	/// Yields whether two slots on the current stack are same, respecting stack size limits
	bool isSourceCompatible(StackOffset _sourceOffset1, StackOffset _sourceOffset2) const;
	/// Checks if swapping the current offset with top makes progress toward target
	bool isSafeToSwapWithTop(StackOffset _offset) const;
	/// Shuffling target information
	Target const& target() const;

	/// A range of offsets `[argsBegin, argsEnd)` intersected with the current stack size
	auto stackArgsRange() const
	{
		return ranges::views::iota(std::min(m_target.tailSize, m_stackData.size()), std::min(m_target.size, m_stackData.size())) | ranges::views::transform([](auto _i) { return StackOffset{_i}; });
	}

	/// A range of offsets `[0, argsBegin)` intersected with the current stack size
	auto stackTailRange() const
	{
		return ranges::views::iota(0u, std::min(m_target.tailSize, m_stackData.size())) | ranges::views::transform([](auto _i) { return StackOffset{_i}; });
	}

	/// A range of offsets `[0, stackSize)`
	auto stackRange() const
	{
		return ranges::views::iota(0u, m_stackData.size()) | ranges::views::transform([&](auto _i) { return StackOffset{_i}; });
	}

	/// A reversed range of offsets `[stackSize - reachableStackDepth - 1, stackSize)`
	auto stackSwapReachableRange() const
	{
		return ranges::views::iota(0u, std::min(m_stackData.size(), m_reachableStackDepth + 1)) | ranges::views::transform([&](auto _i) { return StackOffset{m_stackData.size() - _i - 1}; });
	}

	/// A reversed range of offsets `[stackSize - reachableStackDepth - 1, stackSize)`
	auto stackDupReachableRange() const
	{
		return ranges::views::iota(0u, std::min(m_stackData.size(), m_reachableStackDepth)) | ranges::views::transform([&](auto _i) { return StackOffset{m_stackData.size() - _i - 1}; });
	}

	/// Depth of the deepest arg slot incompatible with target or Nothing for no incompatibility in current state
	std::optional<StackDepth> findDeepestIncorrectArgSlot() const;

	bool slotCanBeLoadedOrPushed(StackSlot const& _slot) const
	{
		return detail::slotCanBeLoadedOrPushed(_slot, m_slots);
	}

	bool slotIsSpilled(StackSlot const& _slot) const
	{
		return detail::slotIsSpilled(_slot, m_slots);
	}

private:
	StackData const& m_stackData;
	Target const& m_target;
	spill::SpillSet const* const m_slots;
	std::size_t const m_reachableStackDepth;
	boost::container::flat_map<StackSlot, size_t> m_histogramTail;
	boost::container::flat_map<StackSlot, size_t> m_histogramArgs;
	boost::container::flat_map<StackSlot, size_t> m_histogramReachable;
	boost::container::flat_map<StackSlot, size_t> m_histogram;
};
}

struct StackShufflerResult
{
	enum class Status { Continue, Admissible, StackTooDeep, MaxIterationsReached };
	Status status = Status::Admissible;
	StackSlot culprit = StackSlot::makeJunk();
};

/// Greedy stack shuffler that drives the EVM stack from a source layout into a target
/// layout via SWAP/DUP/POP/PUSH (and, when a spill set is provided, MLOAD).
///
/// Invariance contract (partial): heuristic decisions in this shuffler — `shrinkStack`'s
/// pop priority, `fixTailSlot`'s "expendable" swap loop, and the last-resort top pop —
/// are spill-set-independent. Two state-function inputs intentionally retain spill-set
/// awareness because SLG depends on them for realization feasibility:
///
///   * `Target::minCount` skips spilled values (they don't contribute to required counts).
///   * `State::requiredInTail` returns false for spilled values (they don't need tail
///     slots; the codegen MLOADs them on demand).
///
/// Consequence: for spill assignments `S ⊆ S'`, traces agree on every choice that doesn't
/// transit through one of those two state functions. In practice this kills the
/// most-common divergence source (heuristic shrink/expendability flipping based on spill
/// membership) and the oscillation patterns it produced, but doesn't promise strict
/// invariance across all spill-set augmentations. The property test
/// `StackShufflerInvariance` verifies this in the common cases.
///
/// MLOAD itself only appears at structural leaves where a target slot is needed and
/// neither reachable via SWAP/DUP nor freely generatable as a literal/junk.
template<StackManipulationCallbackConcept Callback, std::size_t ReachableStackDepth=16>
class StackShuffler
{
	using Slot = StackSlot;

public:
	[[nodiscard]] static StackShufflerResult shuffle(
		Stack<Callback>& _stack,
		StackData const& _args,
		StackSlotLiveness const& _liveOut,
		std::size_t _targetStackSize,
		spill::SpillSet const* const _slots = nullptr
	)
	{
		detail::Target const target(_args, _liveOut, _targetStackSize, _slots);
		// If the caller has wired up a spill set, the shuffler can reduce the effective liveOut
		// size by spilling; otherwise the liveOut must fit into the target up front.
		if (!_slots)
			yulAssert(_liveOut.size() <= target.size, "not enough tail space");
		{
			// check that all required values are on stack
			detail::State const state(_stack.data(), target, _slots, ReachableStackDepth);
			for (auto const& liveSlot: _liveOut | ranges::views::keys)
				yulAssert(
					!_stack.canBeFreelyGenerated(liveSlot) &&
					(ranges::contains(_stack.data(), liveSlot) || detail::slotIsSpilled(liveSlot, _slots))
				);
			for (auto const& arg: _args)
				yulAssert(detail::slotCanBeLoadedOrPushed(arg, _slots) || ranges::contains(_stack.data(), arg));
		}

		static std::size_t constexpr maxIterations = 1000;
		std::size_t i = 0;
		while (true)
		{
			detail::State const state(_stack.data(), target, _slots, ReachableStackDepth);
			auto result = shuffleStep(_stack, state);
			if (result.status == StackShufflerResult::Status::Admissible)
			{
				yulAssert(state.admissible());
				return result;
			}
			if (result.status == StackShufflerResult::Status::StackTooDeep)
				return result;
			yulAssert(result.status == StackShufflerResult::Status::Continue);
			++i;
			if (i == maxIterations)
			{
				result.status = StackShufflerResult::Status::MaxIterationsReached;
				return result;
			}
		}
		yulAssert(false);
	}

	[[nodiscard]] static StackShufflerResult shuffle(
		Stack<Callback>& _stack,
		StackData const& _target,
		spill::SpillSet const* const _spilledVariables = nullptr
	)
	{
		return shuffle(_stack, _target, {}, _target.size(), _spilledVariables);
	}

private:
	struct ShuffleHelperResult
	{
		enum class Status { NoAction, StackModified, StackTooDeep };
		Status status = Status::NoAction;
		StackSlot culprit = StackSlot::makeJunk();
	};

	/// Walks the stack from top toward bottom and returns the topmost slot that is a non-literal
	/// value and not already in the spill set. Used when a shuffleStep early-exit needs to surface
	/// a culprit for findOptimalTarget's outer spill-discovery loop: the natural site-specific
	/// "trapped" slot (typically `_stack.top()`) may itself already be spilled under the strict
	/// invariance contract, since the shrinkStack heuristics no longer prefer spilled-on-stack
	/// values as pop victims. yulAsserts if no candidate exists, which indicates a trap that
	/// further spilling cannot resolve — typically a stack-too-large condition that needs a
	/// larger target size at a higher level.
	static StackSlot nonSpilledTopmostCulprit(Stack<Callback> const& _stack, detail::State const& _state)
	{
		for (StackOffset const offset: _state.stackRange() | ranges::views::reverse)
		{
			Slot const& candidate = _stack[offset];
			if (
				candidate.isValue() &&
				!candidate.isLiteralValue() &&
				!_state.slotIsSpilled(candidate)
			)
				return candidate;
		}
		yulAssert(false, "no spillable culprit on stack — trap not resolvable by spilling alone");
	}

	/// Make a local step in stack space that should bring us closer to the target.
	static StackShufflerResult shuffleStep(Stack<Callback>& _stack, detail::State const& _state)
	{
		// if the stack is too large, we try to shrink it
		if (_stack.size() > _state.target().size)
		{
			if (shrinkStack(_stack, _state))
				return {StackShufflerResult::Status::Continue};
			// couldn't shrink to required size, need to spill to memory or increase target size
			return {StackShufflerResult::Status::StackTooDeep, nonSpilledTopmostCulprit(_stack, _state)};
		}
		yulAssert(_stack.size() <= _state.target().size, "I1 violated: Stack size too large");

		if (_state.willRequireShrinking())
			if (shrinkStack(_stack, _state))
				return {StackShufflerResult::Status::Continue};

		// after this, all current slots are either in acceptable positions or at least dup-reachable
		if (auto culprit = allNecessarySlotsReachableOrFinal(_stack, _state))
		{
			// !allNecessarySlotsReachableOrFinal(ops) ≡ ¬(∀s: reachable(s) ∨ final(s)) ≡ ∃s: ¬reachable(s) ∧ ¬final(s)
			if (shrinkStack(_stack, _state))
				return {StackShufflerResult::Status::Continue};

			return {StackShufflerResult::Status::StackTooDeep, nonSpilledTopmostCulprit(_stack, _state)};
		}

		// this will either grow the tail as needed, swap down something from args that needs to be in the tail,
		// or report NoAction when there's nothing to be done
		if (auto result = fixTailSlot(_stack, _state); result.status != ShuffleHelperResult::Status::NoAction)
		{
			if (result.status == ShuffleHelperResult::Status::StackTooDeep)
				return {StackShufflerResult::Status::StackTooDeep, result.culprit};
			return {StackShufflerResult::Status::Continue};
		}

		// fixing tail slot fills up the tail so that now the stack must reach into the args region but also not
		// exceed it as per our first invariant
		yulAssert(_state.target().tailSize <= _stack.size() && _stack.size() <= _state.target().size);

		// if the stack reaches into the args region try fixing a slot in there until there's nothing left to be fixed
		// within the target size constraints
		if (auto result = fixArgsSlot(_stack, _state); result.status != ShuffleHelperResult::Status::NoAction)
		{
			if (result.status == ShuffleHelperResult::Status::StackTooDeep)
				return {StackShufflerResult::Status::StackTooDeep, result.culprit};
			return {StackShufflerResult::Status::Continue};
		}

		// if there are no args, we should be done now
		if (_state.target().args.empty())
			return {StackShufflerResult::Status::Admissible};
		// fixArgsSlot may return NoAction even when stack is below target size — its push paths
		// are guarded so that they don't lock in a misaligned prefix. In that case we fall through
		// to shrinkStack below, which tears down enough of the stack for the rebuild to make
		// progress on the next iteration.
		yulAssert(_stack.size() <= _state.target().size);

		// check whether we are done
		if (_state.admissible())
			return {StackShufflerResult::Status::Admissible};

		// We couldn't improve the args tail or args situation, and we are not admissible yet, so try to reduce the
		// stack size and pop something that we don't need so we make space to dup/push stuff within target size
		if (shrinkStack(_stack, _state))
			return {StackShufflerResult::Status::Continue};

		// if we couldn't shrink the stack we surface this failed state as stack too deep
		return {StackShufflerResult::Status::StackTooDeep, nonSpilledTopmostCulprit(_stack, _state)};
	}

	/// Select an optimal slot to dup based on liveness analysis.
	/// Prioritizes slots that have the highest deficit with respect to liveOut counts.
	static std::optional<StackDepth> selectOptimalSlotToDup(Stack<Callback> const& _stack, detail::State const& _state)
	{
		std::optional<StackDepth> bestSlot;
		int bestDeficit = 0; // Only consider positive deficits

		// Iterate through all slots on the stack that can be DUPed
		for (StackOffset offset: _state.stackDupReachableRange() | ranges::views::reverse)
		{
			Slot const& slot = _stack[offset];

			// Skip junk slots
			if (slot.isJunk())
				continue;

			// Calculate deficit: how many more of this slot do we need?
			// Uses the deficit of slots which we need to produce more of based on usage counts in liveness.
			// Prioritizes slots that need more copies to be consumed down the line.
			int currentCount = static_cast<int>(_state.count(slot));

			int liveOutCount = 0;
			if (slot.isValue() && _state.target().liveOut.contains(slot))
				liveOutCount = static_cast<int>(_state.target().liveOut.count(slot));
			int deficit = liveOutCount - currentCount;

			// Update best if this deficit is higher
			if (deficit > bestDeficit)
			{
				bestDeficit = deficit;
				bestSlot = _stack.offsetToDepth(offset);
			}
		}

		return bestSlot;
	}

	/// Dups the deepest reachable slot in the tail that is required in args
	static ShuffleHelperResult dupDeepestRelevantTailSlot(Stack<Callback>& _stack, detail::State const& _state)
	{
		// dup up the deepest slot that is required in args (or compress if unreachable)
		for (StackOffset offset: _state.stackRange())
		{
			// if we need the slot in args and there's no slot of the same kind further up
			if (
				_state.requiredInArgs(_stack[offset]) &&
				ranges::find(ranges::begin(_stack) + static_cast<std::ptrdiff_t>(offset.value) + 1, ranges::end(_stack), _stack[offset]) == ranges::end(_stack)
			)
			{
				// dup if we can
				if (_stack.dupReachable(offset))
				{
					_stack.dup(offset);
					return {ShuffleHelperResult::Status::StackModified};
				}

				// try to compress
				if (shrinkStack(_stack, _state))
					return {ShuffleHelperResult::Status::StackModified};

				// Under the invariance contract a spilled trapped slot should have been handled by
				// the leaf push path before reaching this trap.
				yulAssert(!_state.slotIsSpilled(_stack[offset]), "trapped slot in dupDeepestRelevantTailSlot is already spilled");
				return {ShuffleHelperResult::Status::StackTooDeep, _stack[offset]};
			}
		}
		return {ShuffleHelperResult::Status::NoAction};
	}

	/// If dupping an ideal slot causes a slot that will still be required to become unreachable, then dup
	/// the latter slot first
	static ShuffleHelperResult dupDeepSlotIfRequired(Stack<Callback>& _stack, detail::State const& _state)
	{
		// Check if the stack is large enough for anything to potentially become unreachable.
		if (_stack.size() < ReachableStackDepth - 1)
			return {ShuffleHelperResult::Status::NoAction};
		// Check whether any deep slot might still be needed later (i.e. we still need to reach it with a DUP or SWAP).
		for (StackOffset sourceOffset{0u}; sourceOffset < _stack.size() - (ReachableStackDepth - 1); ++sourceOffset.value)
		{
			// This slot needs to be moved into args and there is no tail slot of the same kind further up in the stack.
			auto const& endangeredSlot = _stack[sourceOffset];
			// no need to dup deep junk
			if (endangeredSlot.isJunk())
				continue;
			bool const neededInArgs = _state.targetArgsCount(endangeredSlot) > _state.countInArgs(endangeredSlot);
			bool const needMore = _state.targetMinCount(endangeredSlot) > _state.count(endangeredSlot);
			if (!neededInArgs && !needMore)
				continue;
			// if we ever need more of a slot then this can only happen if it is something we require in the arguments
			yulAssert(_state.requiredInArgs(endangeredSlot));
			// if there's a shallower slot with the same info that is reachable, skip this one
			std::optional<StackDepth> depth = _stack.findSlotDepth(endangeredSlot);
			yulAssert(depth);
			bool const haveMoreAbove = *depth < _stack.offsetToDepth(sourceOffset);
			if (haveMoreAbove)
				continue;

			if (_stack.dupReachable(sourceOffset))
			{
				// if we can safely swap the current stack top with the endangered slot, we do that instead of DUP
				if (_state.isSafeToSwapWithTop(sourceOffset))
				{
					// top can go into the tail bit, swap it down
					_stack.swap(sourceOffset);
					return {ShuffleHelperResult::Status::StackModified};
				}
				else
				{
					// we need more of the slot that is about to go out of reach, dup it
					_stack.dup(sourceOffset);
					return {ShuffleHelperResult::Status::StackModified};
				}
			}
			else
			{
				// even if it is not dup reachable, it still might be swappable
				if (_stack.isValidSwapTarget(sourceOffset) && _state.isSafeToSwapWithTop(sourceOffset))
				{
					_stack.swap(sourceOffset);
					return {ShuffleHelperResult::Status::StackModified};
				}
				// the slot we need something in the args region of is unreachable, try compressing the stack,
				// first looking at the top
				if (shrinkStack(_stack, _state))
					return {ShuffleHelperResult::Status::StackModified};

				// Under the invariance contract a spilled endangered slot should have been handled
				// by the leaf push path before reaching this trap.
				yulAssert(!_state.slotIsSpilled(_stack.slot(*depth)), "endangered slot in dupDeepSlotIfRequired is already spilled");
				return {ShuffleHelperResult::Status::StackTooDeep, _stack.slot(*depth)};
			}
		}
		return {ShuffleHelperResult::Status::NoAction};
	}

	/// Whether the args-region prefix below `_upperBound` is "clean enough" to allow a push at
	/// `offset = _upperBound`. A push grows the stack by 1, so an offset reachable today becomes
	/// one slot deeper after; we therefore require every arg slot below that would be *unreachable
	/// after the push* to already be in position. Reachable post-push misalignments can still be
	/// fixed by SWAPs.
	///
	/// Without this guard, when every reachable slot is spilled and a deep arg slot is misaligned,
	/// `fixArgsSlot` keeps re-pushing missing args while `shrinkStack`'s teardown branch keeps
	/// popping them — oscillation. With it, pushes are blocked until the prefix has been torn
	/// down enough for the rebuild to proceed cleanly.
	static bool argsPrefixIsClean(Stack<Callback> const& _stack, detail::State const& _state, StackOffset _upperBound)
	{
		// post-push depth of offset p is `_stack.size() - p`; out of swap range when > ReachableStackDepth.
		for (StackOffset p{_state.target().tailSize}; p < _upperBound; ++p.value)
			if (_stack.size() - p.value > ReachableStackDepth && !_state.isArgsCompatible(p, p))
				return false;
		return true;
	}

	/// Tries to fix a slot in the args section of the stack
	static ShuffleHelperResult fixArgsSlot(Stack<Callback>& _stack, detail::State const& _state)
	{
		yulAssert(_stack.size() <= _state.target().size, "this method assumes that the stack isn't too large");
		if (_stack.size() < _state.target().tailSize)
			return {ShuffleHelperResult::Status::NoAction};

		StackOffset const stackTop{_stack.size() - 1};
		// if we have at least one slot in the args section, try to fix something there
		if (_stack.size() > _state.target().tailSize)
		{
			// if the stack top isn't where it likes to be right now, try to put it somewhere more sensible
			if (!_state.isArgsCompatible(stackTop, stackTop))
			{
				yulAssert(!_state.requiredInTail(_stack[stackTop]) || _state.countInTail(_stack[stackTop]) > 0);
				// try finding a slot that is compatible with the top and also admits the current top:
				//		- could be that the top slot is used elsewhere in the args (exclude junk)
				//		- could be that the top slot is something that is only required in the tail
				for (StackOffset offset: _state.stackArgsRange())
					if (
						offset != stackTop &&
						_stack[offset] != _stack[stackTop] &&  // don't swap identical values (no-op)
						_stack.isValidSwapTarget(offset) &&
						_state.isArgsCompatible(offset, stackTop) &&
						_state.isArgsCompatible(stackTop, offset) &&
						!_state.targetArbitrary(offset)
					)
					{
						_stack.swap(offset);
						return {ShuffleHelperResult::Status::StackModified};
					}

				// try finding a slot in args that wants to have the top, swap that
				for (StackOffset offset: _state.stackArgsRange())
					if (
						offset != stackTop &&
						_stack[offset] != _stack[stackTop] &&  // don't swap identical values (no-op)
						_stack.isValidSwapTarget(offset) &&
						!_state.isArgsCompatible(offset, offset) &&
						_state.isArgsCompatible(stackTop, offset)
					)
					{
						_stack.swap(offset);
						return {ShuffleHelperResult::Status::StackModified};
					}

				// try swapping top with a tail slot that has what we need at top
				for (StackOffset tailOffset: _state.stackTailRange())
					if (
						_stack.isValidSwapTarget(tailOffset) &&
						_state.isArgsCompatible(tailOffset, stackTop) &&
						(!_state.requiredInTail(_stack[tailOffset]) || _state.countInTail(_stack[tailOffset]) > 1) &&
						// current top can safely go to tail (not needed in args, or we have excess)
						(
							!_state.requiredInArgs(_stack[stackTop]) ||
							_state.countInArgs(_stack[stackTop]) > _state.targetArgsCount(_stack[stackTop])
						)
					)
					{
						_stack.swap(tailOffset);
						return {ShuffleHelperResult::Status::StackModified};
					}
			}

			// swap up any slot in args that is out of position and has a slot available in args that it can occupy
			for (StackOffset offset: _state.stackArgsRange())
			{
				// when offset is already top no swap-up is needed, so it doesn't have to be a valid swap target itself
				bool const reachable = !_stack.isBeyondSwapRange(offset);
				bool const identical = _state.isArgsCompatible(offset, stackTop) && !_state.targetArbitrary(stackTop);
				if (
					reachable &&
					!identical && // we wouldn't just be swapping identical things
					(
						!_state.isArgsCompatible(offset, offset) || // the slot at offset isn't final
						(_state.targetArbitrary(offset) && !_stack.slot(offset).isJunk()) // or the target is arbitrary and the current slot isn't already junk
					)
				)
				{
					// for each `targetOffset` in stack args range, see if we can't swap the out of position `offset` to `targetOffset`
					for (StackOffset targetOffset: _state.stackArgsRange())
						if (
							targetOffset != offset &&  // we shouldn't be looking at the very same offset
							_stack.isValidSwapTarget(targetOffset) &&  // the target offset should be within reach
							_state.isArgsCompatible(offset, targetOffset) &&  // we can put offset -> targetOffset
							!_state.isArgsCompatible(targetOffset, targetOffset)  // targetOffset doesn't like where it is
						)
						{
							if (offset != stackTop)
							{
								// swap up slot at offset
								_stack.swap(offset);
							}
							// bring slot at offset into fixed position
							_stack.swap(targetOffset);
							return {ShuffleHelperResult::Status::StackModified};
						}
				}

				if (!_state.targetArbitrary(offset) && _stack.isValidSwapTarget(offset))
				{
					// for each `argOffset` in the stack args range, see if we can swap something into `offset`; reverse to prioritize shallow slots
					for (StackOffset argOffset: _state.stackArgsRange() | ranges::views::reverse)
					{
						if (
							!_state.isSourceCompatible(offset, argOffset) &&  // we're not looking at the same thing
							!_stack.isBeyondSwapRange(argOffset) &&  // the target offset should not be beyond reach
							_state.isArgsCompatible(argOffset, offset) && // we can put argOffset -> offset
							_state.countReachable(_stack[argOffset]) > 1 &&  // we still have another reachable copy so a subsequent dup is recoverable
							(  // we only get a strict improvement if
								!_state.isArgsCompatible(argOffset, argOffset) ||  // either the argOffset isn't in position anyway
								_stack.offsetToDepth(offset).value == ReachableStackDepth  // or offset is at the swap edge
							)
						)
						{
							if (argOffset != stackTop)
							{
								// swap up slot at offset
								_stack.swap(argOffset);
							}
							// bring slot at offset into fixed position
							_stack.swap(offset);
							return {ShuffleHelperResult::Status::StackModified};
						}
					}
				}
			}
			// If there were no other swapping opportunities, try fixing at least the top before we start pushing
			// more stuff on stack
			if (!_state.isArgsCompatible(stackTop, stackTop))
				for (StackOffset offset: _state.stackArgsRange())
					if (
						offset != stackTop &&
						_stack[offset] != _stack[stackTop] &&  // don't swap identical values (no-op)
						_stack.isValidSwapTarget(offset) &&
						!_state.isArgsCompatible(offset, offset) &&
						_state.isArgsCompatible(offset, stackTop)
					)
					{
						_stack.swap(offset);
						return {ShuffleHelperResult::Status::StackModified};
					}
		}

		// dup up whatever is missing
		if (_stack.size() < _state.target().size)
		{
			if (auto result = dupDeepSlotIfRequired(_stack, _state); result.status != ShuffleHelperResult::Status::NoAction)
				return result;

			auto const maybeIncorrectArgSlotDepth = _state.findDeepestIncorrectArgSlot();
			if (!maybeIncorrectArgSlotDepth || maybeIncorrectArgSlotDepth->value < ReachableStackDepth - 1)
			{
				StackOffset const targetOffset{_stack.size()};
				if (_state.count(_state.targetArg(targetOffset)) < _state.targetMinCount(_state.targetArg(targetOffset)))
				{
					auto const sourceDepth = _stack.findSlotDepth(_state.targetArg(targetOffset));
					if (!sourceDepth)
					{
						if (argsPrefixIsClean(_stack, _state, targetOffset))
						{
							_stack.push(_state.targetArg(targetOffset));
							return {ShuffleHelperResult::Status::StackModified};
						}
						// Prefix is misaligned; fall through to the deepest-arg loop, which is
						// also guarded and will likewise skip pushing — letting shrinkStack tear
						// the prefix down.
					}
					else
					{
						if (!_stack.dupReachable(*sourceDepth))
						{
							// If the arg is spilled-on-stack, the leaf push path would have MLOADed
							// it via the `!sourceDepth` branch above — but we got here because it
							// IS on stack. Under the invariance contract this means further spilling
							// of this arg won't help; the trap is structural (deep on stack).
							yulAssert(!_state.slotIsSpilled(_state.targetArg(targetOffset)), "deep on-stack arg in fixArgsSlot is already spilled");
							return {ShuffleHelperResult::Status::StackTooDeep, _state.targetArg(targetOffset)};
						}
						_stack.dup(*sourceDepth);
						return {ShuffleHelperResult::Status::StackModified};
					}
				}
			}

			// if we can't directly produce targetOffset, take the deepest arg that we don't have enough of and dup/push that
			// First, prioritize duping args that are on the stack over pushing freely-generatable ones
			bool const canPushAtTop = argsPrefixIsClean(_stack, _state, StackOffset{_stack.size()});
			for (StackOffset offset{_state.target().tailSize}; offset < _state.target().size; ++offset.value)
			{
				Slot const& arg = _state.targetArg(offset);
				// skip this arg, if
				if (
					arg.isJunk() ||  // .. the target arg is junk, it doesn't matter what slot occupies it, skip
					_state.isArgsCompatible(offset, offset) ||  // .. it's already in place
					(_state.count(arg) >= _state.targetMinCount(arg) && _state.countInArgs(arg) >= _state.targetArgsCount(arg))  // .. we have enough of it
				)
					continue;

				if (auto sourceDepth = _stack.findSlotDepth(arg))
				{
					if (_stack.dupReachable(*sourceDepth))
					{
						_stack.dup(*sourceDepth);
						return {ShuffleHelperResult::Status::StackModified};
					}
					if (!_state.slotCanBeLoadedOrPushed(arg))
						return {ShuffleHelperResult::Status::StackTooDeep, arg};
				}
				yulAssert(_state.slotCanBeLoadedOrPushed(arg));
				if (!canPushAtTop)
					continue;
				_stack.push(arg);
				return {ShuffleHelperResult::Status::StackModified};
			}

			// Try to dup the optimal slot based on liveness analysis
			if (auto slotToDup = selectOptimalSlotToDup(_stack, _state))
			{
				_stack.dup(*slotToDup);
				return {ShuffleHelperResult::Status::StackModified};
			}
			// If no suitable slot found, push junk — but only if the prefix below is clean.
			// Otherwise return NoAction so the outer shuffleStep can run shrinkStack and unwind.
			if (!canPushAtTop)
				return {ShuffleHelperResult::Status::NoAction};
			_stack.push(Slot::makeJunk());
			return {ShuffleHelperResult::Status::StackModified};
		}

		// if we're at size and have to push or dup something to satisfy args
		if (_stack.size() == _state.target().size)
		{
			for (auto const& arg: _state.target().args)
				if (_state.count(arg) < _state.targetMinCount(arg))
				{
					// we have asserted that all relevant slots are reachable or final, so the arg must either be
					// within dup-reach or we can just push it
					if (auto depth = _stack.findSlotDepth(arg))
					{
						if (_stack.isBeyondSwapRange(*depth))
						{
							yulAssert(!_state.slotIsSpilled(arg), "beyond-swap-range arg in fixArgsSlot-at-size is already spilled");
							return {ShuffleHelperResult::Status::StackTooDeep, arg};
						}
						// if we can't outright dup the slot, let's shrink the stack first
						if (!_stack.dupReachable(*depth))
						{
							if (!shrinkStack(_stack, _state))
							{
								yulAssert(!_state.slotIsSpilled(arg), "shrink-failure arg in fixArgsSlot-at-size is already spilled");
								return {ShuffleHelperResult::Status::StackTooDeep, arg};
							}
							return {ShuffleHelperResult::Status::StackModified};
						}
						_stack.dup(*depth);
						return {ShuffleHelperResult::Status::StackModified};
					}
					else
					{
						if (!_state.slotCanBeLoadedOrPushed(arg))
							return {ShuffleHelperResult::Status::StackTooDeep, arg};
						auto result = dupDeepSlotIfRequired(_stack, _state);
						if (result.status == ShuffleHelperResult::Status::StackTooDeep)
							return result;
						if (result.status == ShuffleHelperResult::Status::NoAction)
						{
							// Push at offset = _stack.size() (top); guard the same way as the
							// other push paths so we don't lock in a misaligned prefix.
							if (!argsPrefixIsClean(_stack, _state, StackOffset{_stack.size()}))
								return {ShuffleHelperResult::Status::NoAction};
							_stack.push(arg);
						}
						return {ShuffleHelperResult::Status::StackModified};
					}
				}
		}
		return {ShuffleHelperResult::Status::NoAction};
	}

	/// Grows the tail if too small, otherwise tries swapping something down from args if its required in tail but not
	/// there yet.
	static ShuffleHelperResult fixTailSlot(Stack<Callback>& _stack, detail::State const& _state)
	{
		yulAssert(_stack.size() <= _state.target().size, "this method assumes that the stack isn't exceeding target size");
		for (StackOffset offset: _state.stackArgsRange() | ranges::views::reverse)
		{
			Slot const& slotAtOffset = _stack[offset];
			if (
				_state.requiredInTail(slotAtOffset) &&  // if we need the slot in tail
				_state.countInTail(slotAtOffset) == 0  // if we don't have the slot in tail right now
			)
			{
				// find the lowest swappable slot in tail that needs to go to args, swap
				for (StackOffset tailOffset: _state.stackTailRange())
				{
					auto const& slotAtTailOffset = _stack[tailOffset];
					if (
						_stack.isValidSwapTarget(tailOffset) &&  // we can swap that deep
						(!_state.requiredInTail(slotAtTailOffset) || _state.countInTail(slotAtTailOffset) > 1) &&  // dont need it in tail or it's available more than once
						_state.requiredInArgs(slotAtTailOffset) &&  // we need the tail offset slot in args
						_state.targetArgsCount(slotAtTailOffset) > _state.countInArgs(slotAtTailOffset)  // we don't already have enough of it in args
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_stack.size() - 1})
							_stack.swap(offset);
						// swap offset slot down into tail
						_stack.swap(tailOffset);
						return {ShuffleHelperResult::Status::StackModified};
					}
				}
				// find the lowest swappable slot in tail that is freely generatable but not a
				// literal (junk or function-return-label), swap. Spill-set membership is
				// intentionally NOT consulted here — using it would let later-discovered spills
				// perturb earlier-realized traces.
				for (StackOffset tailOffset: _state.stackTailRange())
					if (
						_stack.isValidSwapTarget(tailOffset) &&
						_stack.canBeFreelyGenerated(_stack[tailOffset]) &&
						!_stack[tailOffset].isLiteralValue()
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_stack.size() - 1})
							_stack.swap(offset);
						// swap offset slot down into tail
						_stack.swap(tailOffset);
						return {ShuffleHelperResult::Status::StackModified};
					}
				// find the lowest swappable slot in tail that is a literal, swap
				for (StackOffset tailOffset: _state.stackTailRange())
					if (
						_stack.isValidSwapTarget(tailOffset) &&
						_stack[tailOffset].isLiteralValue()
					)
					{
						// bring up offset slot if necessary
						if (offset != StackOffset{_stack.size() - 1})
							_stack.swap(offset);
						// swap offset slot down into tail
						_stack.swap(tailOffset);
						return {ShuffleHelperResult::Status::StackModified};
					}
				// we needed to bring the slot into tail but couldn't, not enough stack target space -> spill to memory
				// Under the invariance contract, a spilled slot at this point would have been treated
				// the same as a non-spilled one (requiredInTail no longer skips spilled), so further
				// spilling of this slot won't change the situation — surface a non-spilled candidate.
				yulAssert(!_state.slotIsSpilled(_stack[offset]), "tail-required slot in fixTailSlot is already spilled");
				return {ShuffleHelperResult::Status::StackTooDeep, _stack[offset]};
			}
		}

		if (_stack.size() < _state.target().tailSize)
		{
			// if something is on the verge of going out of scope by duping something, dup that first
			if (auto result = dupDeepSlotIfRequired(_stack, _state); result.status != ShuffleHelperResult::Status::NoAction)
				return result;

			// dup up the deepest slot that needs to go into args so we avoid having to fish it back up later
			if (auto result = dupDeepestRelevantTailSlot(_stack, _state); result.status != ShuffleHelperResult::Status::NoAction)
				return result;

			// Try to dup the optimal slot based on liveness analysis
			if (auto slotToDup = selectOptimalSlotToDup(_stack, _state))
				_stack.dup(*slotToDup);
			else
				// If no suitable slot found, push junk
				_stack.push(Slot::makeJunk());
			return {ShuffleHelperResult::Status::StackModified};
		}
		return {ShuffleHelperResult::Status::NoAction};
	}

	/// Tries to compress the stack
	static bool shrinkStack(Stack<Callback>& _stack, detail::State const& _state)
	{
		yulAssert(!_stack.empty(), "Stack is empty, can't shrink");

		StackOffset const stackTop{_stack.size() - 1};
		// pop top if it is junk (ie actual junk, not in args, not in live out)
		if (
			_stack[stackTop].isJunk() ||
			(!_state.requiredInArgs(_stack[stackTop]) && !_state.requiredInTail(_stack[stackTop]))
		)
		{
			_stack.pop();
			return true;
		}

		// swap top to suitable position, prioritizing args region
		{
			if (_state.requiredInArgs(_stack[stackTop]))
			{
				for (StackOffset argsOffset: _state.stackArgsRange())
					if (
						_stack[argsOffset] != _stack[stackTop] &&  // don't swap identical values (no-op)
						_stack.isValidSwapTarget(argsOffset) &&
						_state.isArgsCompatible(stackTop, argsOffset) &&
						!_state.isArgsCompatible(argsOffset, argsOffset)
					)
					{
						_stack.swap(argsOffset);
						return true;
					}
			}
			// we don't need it in args but in tail
			if (!_state.requiredInArgs(_stack[stackTop]) && _state.requiredInTail(_stack[stackTop]))
			{
				// pop when at least one of the two conditions is fulfilled
				//	- the top slot is contained in tail, and we're in args or excess region
				//	- there's more than one in tail
				if (
					(
						_state.countInTail(_stack[stackTop]) >= 1 &&
						(_state.offsetInTargetArgsRegion(stackTop) || _stack.size() > _state.target().size)
					) || _state.countInTail(_stack[stackTop]) > 1
				)
				{
					_stack.pop();
					return true;
				}

				// if we need it down there, try to swap down
				for (StackOffset tailOffset: _state.stackTailRange() | ranges::views::reverse)
					if (
						_stack[tailOffset] != _stack[stackTop] &&  // don't swap identical values (no-op)
						_stack.isValidSwapTarget(tailOffset) &&  // we can reach the offset
						!(_state.requiredInTail(_stack[tailOffset]) && _state.countInTail(_stack[tailOffset]) <= 1)  // it's okay to swap the tail offset out
					)
					{
						_stack.swap(tailOffset);
						return true;
					}
			}
		}

		{
			auto const shrinkPriority = [&](StackOffset const _offset) -> std::uint32_t
			{
				auto const& slot = _stack[_offset];
				bool const notInPosition = !_state.isArgsCompatible(_offset, _offset);
				bool const isJunk = slot.isJunk();
				bool const hasSurplus = _state.count(slot) > _state.targetMinCount(slot);
				bool const hasReachableDuplicate = _state.countReachable(slot) > 1;
				// Spill-set membership is intentionally NOT consulted: heuristic priorities must be
				// invariant under spill-set growth so the produced trace is stable across SLG's
				// progressive spill discovery (see shuffler-level invariance note).
				bool const canBeFreelyGenerated = _stack.canBeFreelyGenerated(slot);
				bool const isLit = slot.isLiteralValue();

				if (isJunk && notInPosition)
					return 5;
				if (canBeFreelyGenerated && !isLit && notInPosition)
					return 4;
				if (hasSurplus)
					return 3;
				if (canBeFreelyGenerated)
					return 2;
				if (hasReachableDuplicate)
					return 1;
				return 0;
			};
			std::optional<StackOffset> slotToPop{std::nullopt};
			std::uint32_t bestScore = 0;
			for (StackOffset offset: _state.stackSwapReachableRange())
				if (std::uint32_t const score = shrinkPriority(offset); score > bestScore)
				{
					bestScore = score;
					slotToPop = offset;
				}

			if (slotToPop)
			{
				if (*slotToPop != stackTop && _stack[*slotToPop] != _stack[stackTop])
					_stack.swap(*slotToPop);
				_stack.pop();
				return true;
			}

		}

		// Last-resort teardown: the priority loop above couldn't find a candidate within swap range
		// (e.g. every reachable slot is in args-position and not freely-generatable from the
		// shuffler's POV). If the very top is freely-generatable (literal, junk, or
		// function-return-label), pop it regardless of args-compatibility. The prefix-clean push
		// guard in fixArgsSlot prevents re-installing it until the prefix below is clean, so this
		// cannot oscillate; instead the stack tears down until the misaligned prefix becomes
		// reachable, then rebuilds via push. Spill-set membership is intentionally NOT consulted:
		// popping a spilled-on-stack top here would make the trace depend on a value being in
		// spill, violating the invariance contract.
		if (_stack.canBeFreelyGenerated(_stack[stackTop]))
		{
			_stack.pop();
			return true;
		}

		return false;
	}

	/// Checks if all current slots are either in a position that is compatible with the target or, if not, are
	/// dup-reachable.
	/// Returns the culprit slot (guaranteed to be non-junk) that cannot be placed or duplicated, or `std::nullopt`
	/// if every slot is reachable-or-final.
	static std::optional<StackSlot> allNecessarySlotsReachableOrFinal(Stack<Callback> const& _stack, detail::State const& _state)
	{
		// check that args are either in position or reachable
		for (StackOffset offset{_state.target().tailSize}; offset < _state.target().size; ++offset.value)
		{
			if (_state.isArgsCompatible(offset, offset))
				continue;

			auto const& targetArg = _state.targetArg(offset);
			// if the target arg is junk, we can simply push0 and it's fine
			if (targetArg.isJunk())
				continue;

			// the target offset itself is out of swap range, we must shrink to reach it
			if (offset.value < _stack.size() && _stack.isBeyondSwapRange(offset))
				return targetArg;

			// find first occurrence of the slot
			std::optional<StackDepth> const depth = _stack.findSlotDepth(targetArg);
			if (!depth)
			{
				// if there is no occurrence of the slot anywhere, we must be able to freely generate it
				yulAssert(_state.slotCanBeLoadedOrPushed(targetArg));
			}
			else
			{
				if (_stack.isBeyondSwapRange(*depth) && !_state.slotCanBeLoadedOrPushed(targetArg))
					return targetArg;
			}
		}
		// distribution check: all we have to dup can be duped
		for (StackOffset const offset: _state.stackRange())
		{
			auto const& slotAtOffset = _stack[offset];
			// we don't have enough of the slot
			if (
				_state.count(slotAtOffset) < _state.targetMinCount(slotAtOffset) &&
				!_stack.dupReachable(offset)
			)
			{
				// find first occurrence of the slot
				std::optional<StackDepth> depth = _stack.findSlotDepth(slotAtOffset);
				// it must exist
				yulAssert(depth);
				if (!_stack.dupReachable(*depth) && !_state.slotCanBeLoadedOrPushed(slotAtOffset))
					return slotAtOffset;
			}
		}

		return std::nullopt;
	}
};

[[nodiscard]] inline StackShufflerResult shuffleWithSpillDiscovery(
	StackData& _data,
	StackData const& _args,
	StackSlotLiveness const& _liveOut,
	std::size_t const _targetStackSize,
	spill::SpillSet& _slots
)
{
	StackData const initialData = _data;
	StackShufflerResult result;
	do
	{
		_data = initialData;
		Stack<> stack(_data, {});
		result = StackShuffler<NoOpStackManipulationCallbacks>::shuffle(stack, _args, _liveOut, _targetStackSize, &_slots);
		switch (result.status)
		{
		case StackShufflerResult::Status::Continue:
			yulAssert(false);
		case StackShufflerResult::Status::Admissible:
			break;
		case StackShufflerResult::Status::StackTooDeep:
		{
			yulAssert(result.culprit.isValue() && !result.culprit.isLiteralValue());
			yulAssert(!_slots.isSpilled(result.culprit.value()));
			_slots.add(result.culprit.value());
			break;
		}
		case StackShufflerResult::Status::MaxIterationsReached:
			break;
		}
	}
	while (result.status == StackShufflerResult::Status::StackTooDeep);
	return result;
}

[[nodiscard]] inline StackShufflerResult shuffleWithSpillDiscovery(
	StackData& _data,
	StackData const& _target,
	spill::SpillSet& _slots)
{
	return shuffleWithSpillDiscovery(_data, _target, {}, _target.size(), _slots);
}


}
