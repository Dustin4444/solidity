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

#include <libyul/backends/evm/ssa/transform/Outliner.h>

#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>

#include <range/v3/view/enumerate.hpp>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{
	bool isRevertZeroBlock(SSACFG const& _cfg, BlockId const _bid)
	{
		auto const& block = _cfg.block(_bid);
		if (not block.isTerminationBlock())
			return false;
		if (block.instructions.size() != 1)
			return false;
		auto const& inst = _cfg.inst(block.instructions[0]);
		if (inst.opcode != InstOpcode::BuiltinCall)
			return false;
		auto const& builtin = _cfg.builtinPayload(block.instructions[0]);
		auto const& maybeEVMInstruction = _cfg.evmDialect.builtin(builtin.builtin).instruction;
		if (not maybeEVMInstruction)
			return false;
		if (maybeEVMInstruction.value() != evmasm::Instruction::REVERT)
			return false;
		yulAssert(inst.inputs.size() == 2);
		auto isZero = [&](InstId const id)
		{
			return _cfg.inst(id).isLiteral() && _cfg.literalPayload(id) == 0;
		};
		return isZero(inst.inputs[0]) && isZero(inst.inputs[1]);
	}

	struct BlockIdentifier
	{
		ControlFlowGraphs::FunctionGraphID graphId;
		BlockId blockId;
	};
} // namespace

void transform::runOutliner(ControlFlowGraphs& _cfgs)
{
	// search for basic blocks consisting of `revert(0,0)` only.
	// mark them for outlining
	std::vector<BlockIdentifier> revertZeroBlocks;
	for (auto const& [id, cfg]: _cfgs.functionGraphs | ranges::views::enumerate)
	{
		if (cfg->numBlocks() == 1)
			continue;
		for (BlockId const blockId: cfg->liveBlocks())
			if (isRevertZeroBlock(*cfg, blockId))
				revertZeroBlocks.push_back({
					.graphId = static_cast<ControlFlowGraphs::FunctionGraphID>(id),
					.blockId = blockId
				});
	}
	if (revertZeroBlocks.size() <= 1)
		return;
	{
		// Create new CFG and add it to the collection
		auto const& evmDialect = _cfgs.mainGraph()->evmDialect;
		auto revertZeroCFG = std::make_unique<SSACFG>(evmDialect);
		auto const entryBlockId = revertZeroCFG->makeBlock(nullptr);
		revertZeroCFG->entry = entryBlockId;
		revertZeroCFG->canContinue = false;
		revertZeroCFG->exits.insert(entryBlockId);
		revertZeroCFG->name = "__revert_zero_zero";
		revertZeroCFG->numReturns = 0;
		auto const maybeRevertHandle = evmDialect.findBuiltin("revert");
		yulAssert(maybeRevertHandle.has_value(), "EVM dialect must have revert");
		auto zeroLiteralId = revertZeroCFG->newLiteral(nullptr, 0);
		revertZeroCFG->makeBuiltinCallWithProjections(
			entryBlockId,
			{.builtin = maybeRevertHandle.value(), .literalArguments = {}},
			{zeroLiteralId, zeroLiteralId},
			1
		);
		_cfgs.functionGraphs.push_back(std::move(revertZeroCFG));
	}
	auto const revertFunctionId = static_cast<FunctionGraphID>(_cfgs.functionGraphs.size() - 1);
	for (auto const& [graphId, blockId]: revertZeroBlocks)
	{
		auto& cfg = *_cfgs.functionGraphs.at(graphId);
		auto& block = cfg.block(blockId);
		yulAssert(block.instructions.size() == 1);
		block.instructions.pop_back();
		cfg.makeCallWithProjections(blockId, {.graphID = revertFunctionId, .canContinue = false, .numReturns = 0}, {}, 0);
	}
}
