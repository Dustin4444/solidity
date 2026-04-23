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

#include <libyul/backends/evm/ssa/transform/Cleanup.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

using namespace solidity::yul::ssa;

namespace
{

/// Path-compresses an Identity chain and returns the terminal replacement ValueId.
/// Writes the compressed result into inst.inputs[0] on every node in the chain
/// so subsequent resolves are O(1).
SSACFG::ValueId resolve(SSACFG& _cfg, SSACFG::ValueId _v)
{
	if (!_v.hasValue())
		return _v;
	auto& inst = _cfg.inst(_v.instId());
	if (inst.opcode != Opcode::Identity)
		return _v;
	SSACFG::ValueId const terminal = resolve(_cfg, inst.inputs.at(0));
	inst.inputs[0] = terminal;
	return terminal;
}

}

void transform::cleanup(SSACFG& _cfg)
{
	// Resolve every ValueId operand to its terminal replacement.
	for (SSACFG::BlockId::ValueType bv = 0; bv < _cfg.numBlocks(); ++bv)
	{
		auto& block = _cfg.block(SSACFG::BlockId{bv});
		for (SSACFG::InstId const instId: block.instructions)
		{
			auto& inst = _cfg.inst(instId);
			// Rewrite inputs of every instruction (including Identity's own inputs[0],
			// which resolve() has already path-compressed).
			for (auto& input: inst.inputs)
				input = resolve(_cfg, input);
		}
		// Rewrite terminator fields.
		std::visit(util::GenericVisitor{
			[](SSACFG::BasicBlock::MainExit&) {},
			[](SSACFG::BasicBlock::Jump&) {},
			[&](SSACFG::BasicBlock::ConditionalJump& c) {
				c.condition = resolve(_cfg, c.condition);
			},
			[&](SSACFG::BasicBlock::FunctionReturn& r) {
				for (auto& v: r.returnValues)
					v = resolve(_cfg, v);
			},
			[](SSACFG::BasicBlock::Terminated&) {}
		}, block.exit);
	}

	// Remove Identity Insts from every block's instruction list.
	for (SSACFG::BlockId::ValueType bv = 0; bv < _cfg.numBlocks(); ++bv)
		std::erase_if(
			_cfg.block(SSACFG::BlockId{bv}).instructions,
			[&_cfg](SSACFG::InstId id) { return _cfg.inst(id).opcode == Opcode::Identity; }
		);
}
