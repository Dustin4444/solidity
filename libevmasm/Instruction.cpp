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
/** @file Instruction.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libevmasm/Instruction.h>

#include <algorithm>

using namespace solidity::evmasm;

namespace
{

struct NamedInstruction
{
	std::string_view name;
	Instruction instruction;
};

// Sorted alphabetically by name for binary search.
constexpr std::array<NamedInstruction, 164> c_namedInstructions = {{
	{"ADD", Instruction::ADD},
	{"ADDMOD", Instruction::ADDMOD},
	{"ADDRESS", Instruction::ADDRESS},
	{"AND", Instruction::AND},
	{"BALANCE", Instruction::BALANCE},
	{"BASEFEE", Instruction::BASEFEE},
	{"BLOBBASEFEE", Instruction::BLOBBASEFEE},
	{"BLOBHASH", Instruction::BLOBHASH},
	{"BLOCKHASH", Instruction::BLOCKHASH},
	{"BYTE", Instruction::BYTE},
	{"CALL", Instruction::CALL},
	{"CALLCODE", Instruction::CALLCODE},
	{"CALLDATACOPY", Instruction::CALLDATACOPY},
	{"CALLDATALOAD", Instruction::CALLDATALOAD},
	{"CALLDATASIZE", Instruction::CALLDATASIZE},
	{"CALLER", Instruction::CALLER},
	{"CALLF", Instruction::CALLF},
	{"CALLVALUE", Instruction::CALLVALUE},
	{"CHAINID", Instruction::CHAINID},
	{"CLZ", Instruction::CLZ},
	{"CODECOPY", Instruction::CODECOPY},
	{"CODESIZE", Instruction::CODESIZE},
	{"COINBASE", Instruction::COINBASE},
	{"CREATE", Instruction::CREATE},
	{"CREATE2", Instruction::CREATE2},
	{"DATALOADN", Instruction::DATALOADN},
	{"DELEGATECALL", Instruction::DELEGATECALL},
	{"DIFFICULTY", Instruction::PREVRANDAO},
	{"DIV", Instruction::DIV},
	{"DUP1", Instruction::DUP1},
	{"DUP10", Instruction::DUP10},
	{"DUP11", Instruction::DUP11},
	{"DUP12", Instruction::DUP12},
	{"DUP13", Instruction::DUP13},
	{"DUP14", Instruction::DUP14},
	{"DUP15", Instruction::DUP15},
	{"DUP16", Instruction::DUP16},
	{"DUP2", Instruction::DUP2},
	{"DUP3", Instruction::DUP3},
	{"DUP4", Instruction::DUP4},
	{"DUP5", Instruction::DUP5},
	{"DUP6", Instruction::DUP6},
	{"DUP7", Instruction::DUP7},
	{"DUP8", Instruction::DUP8},
	{"DUP9", Instruction::DUP9},
	{"DUPN", Instruction::DUPN},
	{"EOFCREATE", Instruction::EOFCREATE},
	{"EQ", Instruction::EQ},
	{"EXP", Instruction::EXP},
	{"EXTCALL", Instruction::EXTCALL},
	{"EXTCODECOPY", Instruction::EXTCODECOPY},
	{"EXTCODEHASH", Instruction::EXTCODEHASH},
	{"EXTCODESIZE", Instruction::EXTCODESIZE},
	{"EXTDELEGATECALL", Instruction::EXTDELEGATECALL},
	{"EXTSTATICCALL", Instruction::EXTSTATICCALL},
	{"GAS", Instruction::GAS},
	{"GASLIMIT", Instruction::GASLIMIT},
	{"GASPRICE", Instruction::GASPRICE},
	{"GT", Instruction::GT},
	{"INVALID", Instruction::INVALID},
	{"ISZERO", Instruction::ISZERO},
	{"JUMP", Instruction::JUMP},
	{"JUMPDEST", Instruction::JUMPDEST},
	{"JUMPF", Instruction::JUMPF},
	{"JUMPI", Instruction::JUMPI},
	{"KECCAK256", Instruction::KECCAK256},
	{"LOG0", Instruction::LOG0},
	{"LOG1", Instruction::LOG1},
	{"LOG2", Instruction::LOG2},
	{"LOG3", Instruction::LOG3},
	{"LOG4", Instruction::LOG4},
	{"LT", Instruction::LT},
	{"MCOPY", Instruction::MCOPY},
	{"MLOAD", Instruction::MLOAD},
	{"MOD", Instruction::MOD},
	{"MSIZE", Instruction::MSIZE},
	{"MSTORE", Instruction::MSTORE},
	{"MSTORE8", Instruction::MSTORE8},
	{"MUL", Instruction::MUL},
	{"MULMOD", Instruction::MULMOD},
	{"NOT", Instruction::NOT},
	{"NUMBER", Instruction::NUMBER},
	{"OR", Instruction::OR},
	{"ORIGIN", Instruction::ORIGIN},
	{"PC", Instruction::PC},
	{"POP", Instruction::POP},
	{"PREVRANDAO", Instruction::PREVRANDAO},
	{"PUSH0", Instruction::PUSH0},
	{"PUSH1", Instruction::PUSH1},
	{"PUSH10", Instruction::PUSH10},
	{"PUSH11", Instruction::PUSH11},
	{"PUSH12", Instruction::PUSH12},
	{"PUSH13", Instruction::PUSH13},
	{"PUSH14", Instruction::PUSH14},
	{"PUSH15", Instruction::PUSH15},
	{"PUSH16", Instruction::PUSH16},
	{"PUSH17", Instruction::PUSH17},
	{"PUSH18", Instruction::PUSH18},
	{"PUSH19", Instruction::PUSH19},
	{"PUSH2", Instruction::PUSH2},
	{"PUSH20", Instruction::PUSH20},
	{"PUSH21", Instruction::PUSH21},
	{"PUSH22", Instruction::PUSH22},
	{"PUSH23", Instruction::PUSH23},
	{"PUSH24", Instruction::PUSH24},
	{"PUSH25", Instruction::PUSH25},
	{"PUSH26", Instruction::PUSH26},
	{"PUSH27", Instruction::PUSH27},
	{"PUSH28", Instruction::PUSH28},
	{"PUSH29", Instruction::PUSH29},
	{"PUSH3", Instruction::PUSH3},
	{"PUSH30", Instruction::PUSH30},
	{"PUSH31", Instruction::PUSH31},
	{"PUSH32", Instruction::PUSH32},
	{"PUSH4", Instruction::PUSH4},
	{"PUSH5", Instruction::PUSH5},
	{"PUSH6", Instruction::PUSH6},
	{"PUSH7", Instruction::PUSH7},
	{"PUSH8", Instruction::PUSH8},
	{"PUSH9", Instruction::PUSH9},
	{"RETF", Instruction::RETF},
	{"RETURN", Instruction::RETURN},
	{"RETURNCONTRACT", Instruction::RETURNCONTRACT},
	{"RETURNDATACOPY", Instruction::RETURNDATACOPY},
	{"RETURNDATASIZE", Instruction::RETURNDATASIZE},
	{"REVERT", Instruction::REVERT},
	{"RJUMP", Instruction::RJUMP},
	{"RJUMPI", Instruction::RJUMPI},
	{"SAR", Instruction::SAR},
	{"SDIV", Instruction::SDIV},
	{"SELFBALANCE", Instruction::SELFBALANCE},
	{"SELFDESTRUCT", Instruction::SELFDESTRUCT},
	{"SGT", Instruction::SGT},
	{"SHL", Instruction::SHL},
	{"SHR", Instruction::SHR},
	{"SIGNEXTEND", Instruction::SIGNEXTEND},
	{"SLOAD", Instruction::SLOAD},
	{"SLT", Instruction::SLT},
	{"SMOD", Instruction::SMOD},
	{"SSTORE", Instruction::SSTORE},
	{"STATICCALL", Instruction::STATICCALL},
	{"STOP", Instruction::STOP},
	{"SUB", Instruction::SUB},
	{"SWAP1", Instruction::SWAP1},
	{"SWAP10", Instruction::SWAP10},
	{"SWAP11", Instruction::SWAP11},
	{"SWAP12", Instruction::SWAP12},
	{"SWAP13", Instruction::SWAP13},
	{"SWAP14", Instruction::SWAP14},
	{"SWAP15", Instruction::SWAP15},
	{"SWAP16", Instruction::SWAP16},
	{"SWAP2", Instruction::SWAP2},
	{"SWAP3", Instruction::SWAP3},
	{"SWAP4", Instruction::SWAP4},
	{"SWAP5", Instruction::SWAP5},
	{"SWAP6", Instruction::SWAP6},
	{"SWAP7", Instruction::SWAP7},
	{"SWAP8", Instruction::SWAP8},
	{"SWAP9", Instruction::SWAP9},
	{"SWAPN", Instruction::SWAPN},
	{"TIMESTAMP", Instruction::TIMESTAMP},
	{"TLOAD", Instruction::TLOAD},
	{"TSTORE", Instruction::TSTORE},
	{"XOR", Instruction::XOR},
}};

// Verify sort order at compile time.
consteval bool namedInstructionsSorted()
{
	for (size_t i = 1; i < c_namedInstructions.size(); ++i)
		if (c_namedInstructions[i - 1].name >= c_namedInstructions[i].name)
			return false;
	return true;
}
static_assert(namedInstructionsSorted(), "c_namedInstructions must be sorted alphabetically by name");

// Build the span-compatible array of pairs for allNamedInstructions().
consteval std::array<std::pair<std::string_view, Instruction>, c_namedInstructions.size()> buildNamedInstructionPairs()
{
	std::array<std::pair<std::string_view, Instruction>, c_namedInstructions.size()> result{};
	for (size_t i = 0; i < c_namedInstructions.size(); ++i)
		result[i] = {c_namedInstructions[i].name, c_namedInstructions[i].instruction};
	return result;
}
constexpr auto c_namedInstructionPairs = buildNamedInstructionPairs();

} // anonymous namespace

std::optional<Instruction> solidity::evmasm::instructionByName(std::string_view _name)
{
	auto it = std::lower_bound(
		c_namedInstructions.begin(),
		c_namedInstructions.end(),
		_name,
		[](NamedInstruction const& _entry, std::string_view _n) { return _entry.name < _n; }
	);
	if (it != c_namedInstructions.end() && it->name == _name)
		return it->instruction;
	return std::nullopt;
}

std::span<std::pair<std::string_view, Instruction> const> solidity::evmasm::allNamedInstructions()
{
	return c_namedInstructionPairs;
}
