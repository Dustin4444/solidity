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
/**
 * Unit tests for the shared Cleanup pass.
 */

#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/transform/Cleanup.h>

#include <boost/test/unit_test.hpp>

namespace solidity::yul::ssa::test
{

namespace
{
/// Rewrite `_victim`'s inst to forward to `_replacement` via Identity semantics.
void rewriteToIdentity(SSACFG& _cfg, SSACFG::ValueId _victim, SSACFG::ValueId _replacement)
{
	auto& inst = _cfg.inst(_victim.instId());
	inst.opcode = Opcode::Identity;
	inst.inputs = {_replacement};
}

std::size_t countInstructionsWithOpcode(SSACFG const& _cfg, Opcode _op)
{
	std::size_t count = 0;
	for (SSACFG::BlockId::ValueType bv = 0; bv < _cfg.numBlocks(); ++bv)
		for (SSACFG::InstId const id: _cfg.block(SSACFG::BlockId{bv}).instructions)
			if (_cfg.inst(id).opcode == _op)
				++count;
	return count;
}
}

BOOST_AUTO_TEST_SUITE(CleanupPassTest)

BOOST_AUTO_TEST_CASE(removes_identity_from_block_instructions)
{
	auto const& dialect = EVMDialect::strictAssemblyForEVMObjects(langutil::EVMVersion{}, std::nullopt);
	SSACFG cfg(dialect);
	auto const entry = cfg.makeBlock(nullptr);
	cfg.entry = entry;

	auto const zeroA = cfg.newLiteral(nullptr, u256{0});
	auto const zeroB_value = cfg.newLiteral(nullptr, u256{1}); // distinct value to get a distinct Inst
	rewriteToIdentity(cfg, zeroB_value, zeroA);

	BOOST_REQUIRE_EQUAL(countInstructionsWithOpcode(cfg, Opcode::Identity), 0);
	// Identity insts live outside any block's instruction list (Const Insts don't get pushed
	// into blocks), so we add the victim explicitly to the entry block.
	cfg.block(entry).instructions.push_back(zeroB_value.instId());
	BOOST_REQUIRE_EQUAL(countInstructionsWithOpcode(cfg, Opcode::Identity), 1);

	transform::cleanup(cfg);

	BOOST_CHECK_EQUAL(countInstructionsWithOpcode(cfg, Opcode::Identity), 0);
	// The Identity Inst itself remains in m_insts (no GC), just not in block.instructions.
	BOOST_CHECK(cfg.inst(zeroB_value.instId()).opcode == Opcode::Identity);
}

BOOST_AUTO_TEST_CASE(path_compresses_identity_chains)
{
	auto const& dialect = EVMDialect::strictAssemblyForEVMObjects(langutil::EVMVersion{}, std::nullopt);
	SSACFG cfg(dialect);
	auto const entry = cfg.makeBlock(nullptr);
	cfg.entry = entry;

	// Terminal: a Const(42).
	auto const terminal = cfg.newLiteral(nullptr, u256{42});
	// Chain: c -> b -> a -> terminal. Each link rewritten from a fresh literal Inst.
	auto const a = cfg.newLiteral(nullptr, u256{1});
	auto const b = cfg.newLiteral(nullptr, u256{2});
	auto const c = cfg.newLiteral(nullptr, u256{3});
	rewriteToIdentity(cfg, a, terminal);
	rewriteToIdentity(cfg, b, a);
	rewriteToIdentity(cfg, c, b);

	// Make a BuiltinCall that uses `c` as an input, so cleanup rewrites its operand.
	auto const& addBuiltin = dialect.builtin(*dialect.findBuiltin("add"));
	FunctionCall const& syntheticCall = cfg.ghostCalls.emplace_back(FunctionCall{
		{}, BuiltinName{{}, *dialect.findBuiltin("add")}, {}
	});
	auto const outputs = cfg.makeBuiltinCall(
		entry,
		SSACFG::BuiltinCall{addBuiltin, syntheticCall},
		{c, c},
		1
	);
	auto const addResult = outputs.front();

	// Push the Identity victims into the entry block so cleanup has something to prune.
	cfg.block(entry).instructions.push_back(a.instId());
	cfg.block(entry).instructions.push_back(b.instId());
	cfg.block(entry).instructions.push_back(c.instId());

	transform::cleanup(cfg);

	// Identity nodes are gone from block.instructions.
	BOOST_CHECK_EQUAL(countInstructionsWithOpcode(cfg, Opcode::Identity), 0);
	// The BuiltinCall's inputs now reference `terminal` directly (path-compressed through a, b, c).
	auto const& addInst = cfg.inst(addResult.instId());
	BOOST_REQUIRE_EQUAL(addInst.inputs.size(), 2);
	BOOST_CHECK(addInst.inputs[0] == terminal);
	BOOST_CHECK(addInst.inputs[1] == terminal);
}

BOOST_AUTO_TEST_CASE(rewrites_terminator_operands)
{
	auto const& dialect = EVMDialect::strictAssemblyForEVMObjects(langutil::EVMVersion{}, std::nullopt);
	SSACFG cfg(dialect);
	auto const entry = cfg.makeBlock(nullptr);
	cfg.entry = entry;

	auto const terminal = cfg.newLiteral(nullptr, u256{0});
	auto const victim = cfg.newLiteral(nullptr, u256{1});
	rewriteToIdentity(cfg, victim, terminal);

	// Build a FunctionReturn that returns the victim.
	cfg.block(entry).exit = SSACFG::BasicBlock::FunctionReturn{{victim}};
	cfg.block(entry).instructions.push_back(victim.instId());

	transform::cleanup(cfg);

	auto const& functionReturn = std::get<SSACFG::BasicBlock::FunctionReturn>(cfg.block(entry).exit);
	BOOST_REQUIRE_EQUAL(functionReturn.returnValues.size(), 1);
	BOOST_CHECK(functionReturn.returnValues[0] == terminal);
}

BOOST_AUTO_TEST_SUITE_END()

}
