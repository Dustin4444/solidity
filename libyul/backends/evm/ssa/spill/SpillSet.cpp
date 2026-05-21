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

#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackShuffler.h>
#include <libyul/backends/evm/ssa/StackLayout.h>

#include <deque>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::spill;

namespace
{

/// Build the symbolic stack right after `_value`'s operation completes
StackData computeOperationOut(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	InstId const _value
)
{
	SSACFG::BlockId const block = _cfg.inst(_value).block;
	auto const& blockLayout = _layout[block];
	yulAssert(blockLayout, fmt::format("producer {}'s block has no layout", _value));

	std::size_t opIndex = 0;
	bool found = false;
	for (InstId const id: _cfg.block(block).instructions)
	{
		if (!_cfg.isOperation(id))
			continue;
		if (id == _value)
		{
			found = true;
			break;
		}
		++opIndex;
	}
	yulAssert(found, fmt::format("producer {} not found in its block's instructions", _value));
	yulAssert(opIndex < blockLayout->operationIn.size());

	StackData op = blockLayout->operationIn[opIndex];
	SSACFG::Inst const& inst = _cfg.inst(_value);
	yulAssert(op.size() >= inst.inputs.size(), "operationIn smaller than input count");
	for (std::size_t i = 0; i < inst.inputs.size(); ++i)
		op.pop_back();
	_cfg.forEachOutput(_value, [&](InstId const id) {
		op.push_back(StackSlot::makeValue(_cfg, id));
	});
	return op;
}

/// The symbolic stack the Emitter faces at `_value`'s definition, where its `mstore` fires. Three cases:
/// - a phi: the merged value is materialized on its defining block's `stackIn`, so a single store there covers every incoming edge;
/// - a function argument: it has no producer operation and lives on the function entry stack, where CodeTransform emits `mstore` while the args are still laid out;
/// - any other value: it sits on its producer's `operationOut`.
StackData defStackFor(
	SSACFG const& _cfg,
	SSACFGStackLayout const& _layout,
	InstId const _value
)
{
	if (_cfg.isPhi(_value))
	{
		SSACFG::BlockId const block = _cfg.inst(_value).block;
		yulAssert(block.hasValue(), fmt::format("phi {} has no defining block", _value));
		auto const& blockLayout = _layout[block];
		yulAssert(blockLayout, fmt::format("phi {}'s defining block has no layout", _value));
		return blockLayout->stackIn;
	}
	if (_cfg.isFunctionArg(_value))
	{
		auto const& entryLayout = _layout[_cfg.entry];
		yulAssert(entryLayout, "entry block has no layout for function-arg def-site");
		return entryLayout->stackIn;
	}
	return computeOperationOut(_cfg, _layout, _value);
}

}

void SpillSet::feasilize(SSACFG const& _cfg, SSACFGStackLayout const& _layout)
{
	// work queue over values that are marked for spillage
	std::deque<InstId> queue;
	for (InstId const id: spilledValues())
		queue.push_back(id);

	while (!queue.empty())
	{
		InstId const value = queue.front();
		queue.pop_front();

		StackData const defStack = defStackFor(_cfg, _layout, value);
		ensureDefSiteFeasible(_cfg, value, defStack, queue);
	}
}

void SpillSet::ensureDefSiteFeasible(
	SSACFG const& _cfg,
	InstId const _value,
	StackData const& _defStack,
	std::deque<InstId>& _workQueue)
{
	// Leaf predicate = spill set minus the owner; the shuffle accumulates discovered culprits here.
	SpillSet leafSpills = without(_value);
	// [... defStack ..., valueSlot]
	StackData const target = [&]{
		StackData result;
		result.reserve(_defStack.size() + 1);
		result.insert(result.end(), _defStack.begin(), _defStack.end());
		result.push_back(StackSlot::makeValue(_cfg, _value));
		return result;
	}();
	StackData workStack = _defStack;
	StackShufflerResult const result = shuffleWithSpillDiscovery(workStack, target, leafSpills);
	yulAssert(
		result.status == StackShufflerResult::Status::Admissible,
		fmt::format("def-site store for {} infeasible even after spilling siblings (status={})", _value, static_cast<int>(result.status))
	);

	// - if `_value` is reachable, it can be just DUPed and there shouldn't have been a stack too deep with it
	// - if `_value` is unreachable, there are > reachable stack depth distinct slots strictly above it and the
	//   shuffler heuristics should not pick anything that is already too deep as culprit
	yulAssert(!leafSpills.isSpilled(_value), "spill-aware shuffle reported the owner as its own blocker");

	for (InstId const culprit: leafSpills.spilledValues())
	{
		if (isSpilled(culprit))
			continue;
		add(culprit);
		_workQueue.push_back(culprit);
	}
}

SpillSet SpillSet::without(InstId const _id) const
{
	SpillSet result = *this;
	result.m_values.erase(_id);
	return result;
}
