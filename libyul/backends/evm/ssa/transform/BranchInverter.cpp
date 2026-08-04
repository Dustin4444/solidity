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

#include <libyul/backends/evm/ssa/transform/BranchInverter.h>

#include <libyul/backends/evm/ssa/transform/UseCounts.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/Exceptions.h>

#include <optional>
#include <utility>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

void transform::invertBranches(SSACFG& _cfg)
{
	std::optional<BuiltinHandle> const negationHandle = _cfg.evmDialect.booleanNegationFunctionHandle();
	yulAssert(negationHandle.has_value());
	// Snapshot taken before any rewriting: each peel replaces the exit's read of the `iszero` by a
	// read of its operand, so the counts of all still-live insts stay exact and a count of 1 below
	// remains "the sole use is this exit's condition read".
	transform::UseCounts const useCounts(_cfg);
	for (BlockId const blockId: _cfg.liveBlocks())
	{
		auto* conditionalJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&_cfg.block(blockId).exit);
		if (!conditionalJump)
			continue;

		while (true)
		{
			// Keep branches into termination blocks (reverts, panics) as taken jumps: emitted as
			// fallthroughs they lose their label and the block deduplicator can no longer share
			// them between guard sites, costing a copy of the error block per site.
			if (_cfg.block(conditionalJump->nonZero).isTerminationBlock())
				break;
			InstId const conditionId = conditionalJump->condition;
			if (
				_cfg.kindOf(conditionId) != InstOpcode::BuiltinCall ||
				_cfg.builtinPayload(conditionId).builtin != *negationHandle ||
				_cfg.inst(conditionId).block != blockId ||
				!useCounts.hasSingleUse(conditionId)
			)
				break;

			auto const& inputs = _cfg.inst(conditionId).inputs;
			yulAssert(inputs.size() == 1);
			conditionalJump->condition = inputs.front();
			std::swap(conditionalJump->nonZero, conditionalJump->zero);
			_cfg.replaceWithNop(conditionId);
		}
	}
}
