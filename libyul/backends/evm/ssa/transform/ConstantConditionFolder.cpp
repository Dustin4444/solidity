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

#include <libyul/backends/evm/ssa/transform/ConstantConditionFolder.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/Exceptions.h>

#include <range/v3/algorithm/find.hpp>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

bool transform::foldConstantConditions(SSACFG& _cfg)
{
	bool foldedAny = false;
	for (BlockId const blockId: _cfg.liveBlocks())
	{
		auto& block = _cfg.block(blockId);
		auto const* conditionalJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&block.exit);
		if (!conditionalJump)
			continue;

		InstId const conditionId = _cfg.resolveIdentity(conditionalJump->condition);
		if (!_cfg.isLiteral(conditionId))
			continue;
		bool const condition = _cfg.literalPayload(conditionId) != 0;

		BlockId const taken = condition ? conditionalJump->nonZero : conditionalJump->zero;
		BlockId const dropped = condition ? conditionalJump->zero : conditionalJump->nonZero;

		block.exit = SSACFG::BasicBlock::Jump{taken};
		foldedAny = true;

		if (dropped != taken)
		{
			// Detach the edge blockId -> dropped: drop the predecessor entry and turn the upsilons that fed
			// dropped's phis along this edge into nops (their phi pre-images are gone with the edge).
			auto& droppedEntries = _cfg.block(dropped).entries;
			auto const entry = ranges::find(droppedEntries, blockId);
			yulAssert(entry != droppedEntries.end());
			droppedEntries.erase(entry);

			for (InstId const instId: block.instructions)
				if (_cfg.isUpsilon(instId) && _cfg.inst(_cfg.upsilonPhi(instId)).block == dropped)
					_cfg.replaceWithNop(instId);
		}
	}
	return foldedAny;
}
