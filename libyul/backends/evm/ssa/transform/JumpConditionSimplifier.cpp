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

#include <libyul/backends/evm/ssa/transform/JumpConditionSimplifier.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

namespace solidity::yul::ssa::transform
{
void simplifyNegatedJumpConditions(SSACFG& _cfg)
{
	auto const maybeIsZero = _cfg.evmDialect.booleanNegationFunctionHandle();
	if (!maybeIsZero)
		return;
	auto const booleanNegationHandle = *maybeIsZero;
	for (SSACFG::BlockId blockId{0}; blockId.value < _cfg.numBlocks(); ++blockId.value)
	{
		auto& block = _cfg.block(blockId);
		if (!std::holds_alternative<SSACFG::BasicBlock::ConditionalJump>(block.exit))
			continue;
		auto& conditionalJump = std::get<SSACFG::BasicBlock::ConditionalJump>(block.exit);
		auto const condition = conditionalJump.condition;
		auto const& instruction = _cfg.inst(condition);
		if (instruction.opcode != InstOpcode::BuiltinCall)
			continue;

		if (_cfg.builtinPayload(condition).builtin != booleanNegationHandle)
			continue;
		yulAssert(instruction.inputs.size() == 1);
		auto const arg = instruction.inputs.at(0);
		conditionalJump.condition = arg;
		std::swap(conditionalJump.zero, conditionalJump.nonZero);
	}
}
}

