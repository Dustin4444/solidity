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
/** @file Instruction.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <liblangutil/EVMVersion.h>
#include <liblangutil/Exceptions.h>
#include <libsolutil/Assertions.h>
#include <libsolutil/Common.h>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace solidity::evmasm
{

/// Virtual machine bytecode instruction.
enum class Instruction: uint8_t
{
	STOP = 0x00,              ///< halts execution
	ADD,                      ///< addition operation
	MUL,                      ///< multiplication operation
	SUB,                      ///< subtraction operation
	DIV,                      ///< integer division operation
	SDIV,                     ///< signed integer division operation
	MOD,                      ///< modulo remainder operation
	SMOD,                     ///< signed modulo remainder operation
	ADDMOD,                   ///< unsigned modular addition
	MULMOD,                   ///< unsigned modular multiplication
	EXP,                      ///< exponential operation
	SIGNEXTEND,               ///< extend length of signed integer

	LT = 0x10,                ///< less-than comparison
	GT,                       ///< greater-than comparison
	SLT,                      ///< signed less-than comparison
	SGT,                      ///< signed greater-than comparison
	EQ,                       ///< equality comparison
	ISZERO,                   ///< simple not operator
	AND,                      ///< bitwise AND operation
	OR,                       ///< bitwise OR operation
	XOR,                      ///< bitwise XOR operation
	NOT,                      ///< bitwise NOT operation
	BYTE,                     ///< retrieve single byte from word
	SHL,                      ///< bitwise SHL operation
	SHR,                      ///< bitwise SHR operation
	SAR,                      ///< bitwise SAR operation
	CLZ,                      ///< count of leading zeros in binary representation

	KECCAK256 = 0x20,         ///< compute KECCAK-256 hash

	ADDRESS = 0x30,           ///< get address of currently executing account
	BALANCE,                  ///< get balance of the given account
	ORIGIN,                   ///< get execution origination address
	CALLER,                   ///< get caller address
	CALLVALUE,                ///< get deposited value by the instruction/transaction responsible for this execution
	CALLDATALOAD,             ///< get input data of current environment
	CALLDATASIZE,             ///< get size of input data in current environment
	CALLDATACOPY,             ///< copy input data in current environment to memory
	CODESIZE,                 ///< get size of code running in current environment
	CODECOPY,                 ///< copy code running in current environment to memory
	GASPRICE,                 ///< get price of gas in current environment
	EXTCODESIZE,              ///< get external code size (from another contract)
	EXTCODECOPY,              ///< copy external code (from another contract)
	RETURNDATASIZE = 0x3d,    ///< get size of return data buffer
	RETURNDATACOPY = 0x3e,    ///< copy return data in current environment to memory
	EXTCODEHASH = 0x3f,       ///< get external code hash (from another contract)

	BLOCKHASH = 0x40,         ///< get hash of most recent complete block
	COINBASE,                 ///< get the block's coinbase address
	TIMESTAMP,                ///< get the block's timestamp
	NUMBER,                   ///< get the block's number
	PREVRANDAO,               ///< get randomness provided by the beacon chain
	GASLIMIT,                 ///< get the block's gas limit
	CHAINID,                  ///< get the config's chainid param
	SELFBALANCE,              ///< get balance of the current account
	BASEFEE,                  ///< get the block's basefee
	BLOBHASH = 0x49,          ///< get a versioned hash of one of the blobs associated with the transaction
	BLOBBASEFEE = 0x4a,       ///< get the block's blob basefee

	POP = 0x50,               ///< remove item from stack
	MLOAD,                    ///< load word from memory
	MSTORE,                   ///< save word to memory
	MSTORE8,                  ///< save byte to memory
	SLOAD,                    ///< load word from storage
	SSTORE,                   ///< save word to storage
	JUMP,                     ///< alter the program counter
	JUMPI,                    ///< conditionally alter the program counter
	PC,                       ///< get the program counter
	MSIZE,                    ///< get the size of active memory
	GAS,                      ///< get the amount of available gas
	JUMPDEST,                 ///< set a potential jump destination
	TLOAD = 0x5c,             ///< load word from transient storage
	TSTORE = 0x5d,            ///< save word to transient storage
	MCOPY = 0x5e,             ///< copy between memory areas

	PUSH0 = 0x5f,             ///< place the value 0 on stack
	PUSH1 = 0x60,             ///< place 1 byte item on stack
	PUSH2,                    ///< place 2 byte item on stack
	PUSH3,                    ///< place 3 byte item on stack
	PUSH4,                    ///< place 4 byte item on stack
	PUSH5,                    ///< place 5 byte item on stack
	PUSH6,                    ///< place 6 byte item on stack
	PUSH7,                    ///< place 7 byte item on stack
	PUSH8,                    ///< place 8 byte item on stack
	PUSH9,                    ///< place 9 byte item on stack
	PUSH10,                   ///< place 10 byte item on stack
	PUSH11,                   ///< place 11 byte item on stack
	PUSH12,                   ///< place 12 byte item on stack
	PUSH13,                   ///< place 13 byte item on stack
	PUSH14,                   ///< place 14 byte item on stack
	PUSH15,                   ///< place 15 byte item on stack
	PUSH16,                   ///< place 16 byte item on stack
	PUSH17,                   ///< place 17 byte item on stack
	PUSH18,                   ///< place 18 byte item on stack
	PUSH19,                   ///< place 19 byte item on stack
	PUSH20,                   ///< place 20 byte item on stack
	PUSH21,                   ///< place 21 byte item on stack
	PUSH22,                   ///< place 22 byte item on stack
	PUSH23,                   ///< place 23 byte item on stack
	PUSH24,                   ///< place 24 byte item on stack
	PUSH25,                   ///< place 25 byte item on stack
	PUSH26,                   ///< place 26 byte item on stack
	PUSH27,                   ///< place 27 byte item on stack
	PUSH28,                   ///< place 28 byte item on stack
	PUSH29,                   ///< place 29 byte item on stack
	PUSH30,                   ///< place 30 byte item on stack
	PUSH31,                   ///< place 31 byte item on stack
	PUSH32,                   ///< place 32 byte item on stack

	DUP1 = 0x80,              ///< copies the highest item in the stack to the top of the stack
	DUP2,                     ///< copies the second highest item in the stack to the top of the stack
	DUP3,                     ///< copies the third highest item in the stack to the top of the stack
	DUP4,                     ///< copies the 4th highest item in the stack to the top of the stack
	DUP5,                     ///< copies the 5th highest item in the stack to the top of the stack
	DUP6,                     ///< copies the 6th highest item in the stack to the top of the stack
	DUP7,                     ///< copies the 7th highest item in the stack to the top of the stack
	DUP8,                     ///< copies the 8th highest item in the stack to the top of the stack
	DUP9,                     ///< copies the 9th highest item in the stack to the top of the stack
	DUP10,                    ///< copies the 10th highest item in the stack to the top of the stack
	DUP11,                    ///< copies the 11th highest item in the stack to the top of the stack
	DUP12,                    ///< copies the 12th highest item in the stack to the top of the stack
	DUP13,                    ///< copies the 13th highest item in the stack to the top of the stack
	DUP14,                    ///< copies the 14th highest item in the stack to the top of the stack
	DUP15,                    ///< copies the 15th highest item in the stack to the top of the stack
	DUP16,                    ///< copies the 16th highest item in the stack to the top of the stack

	SWAP1 = 0x90,             ///< swaps the highest and second highest value on the stack
	SWAP2,                    ///< swaps the highest and third highest value on the stack
	SWAP3,                    ///< swaps the highest and 4th highest value on the stack
	SWAP4,                    ///< swaps the highest and 5th highest value on the stack
	SWAP5,                    ///< swaps the highest and 6th highest value on the stack
	SWAP6,                    ///< swaps the highest and 7th highest value on the stack
	SWAP7,                    ///< swaps the highest and 8th highest value on the stack
	SWAP8,                    ///< swaps the highest and 9th highest value on the stack
	SWAP9,                    ///< swaps the highest and 10th highest value on the stack
	SWAP10,                   ///< swaps the highest and 11th highest value on the stack
	SWAP11,                   ///< swaps the highest and 12th highest value on the stack
	SWAP12,                   ///< swaps the highest and 13th highest value on the stack
	SWAP13,                   ///< swaps the highest and 14th highest value on the stack
	SWAP14,                   ///< swaps the highest and 15th highest value on the stack
	SWAP15,                   ///< swaps the highest and 16th highest value on the stack
	SWAP16,                   ///< swaps the highest and 17th highest value on the stack

	LOG0 = 0xa0,              ///< Makes a log entry; no topics.
	LOG1,                     ///< Makes a log entry; 1 topic.
	LOG2,                     ///< Makes a log entry; 2 topics.
	LOG3,                     ///< Makes a log entry; 3 topics.
	LOG4,                     ///< Makes a log entry; 4 topics.

	DATALOADN = 0xd1,         ///< load data from EOF data section

	RJUMP = 0xe0,             ///< relative jump
	RJUMPI = 0xe1,            ///< conditional relative jump
	CALLF = 0xe3,             ///< call function in a EOF code section
	RETF = 0xe4,              ///< return to caller from the code section of EOF container
	JUMPF = 0xe5,             ///< jump to a code section of EOF container without adding a new return stack frame.
	DUPN = 0xe6,              ///< copies a value at the stack depth given as immediate argument to the top of the stack
	SWAPN = 0xe7,             ///< swaps the highest value with a value at a stack depth given as immediate argument
	EOFCREATE = 0xec,         ///< create a new account with associated container code.
	RETURNCONTRACT = 0xee,    ///< return container to be deployed with axiliary data filled in.
	CREATE = 0xf0,            ///< create a new account with associated code
	CALL,                     ///< message-call into an account
	CALLCODE,                 ///< message-call with another account's code only
	RETURN,                   ///< halt execution returning output data
	DELEGATECALL,             ///< like CALLCODE but keeps caller's value and sender
	CREATE2 = 0xf5,           ///< create new account with associated code at address `sha3(0xff + sender + salt + init code) % 2**160`
	EXTCALL = 0xf8,           ///< EOF message-call into an account
	EXTDELEGATECALL = 0xf9,   ///< EOF delegate call
	STATICCALL = 0xfa,        ///< like CALL but disallow state modifications
	EXTSTATICCALL = 0xfb,     ///< like EXTCALL but disallow state modifications

	REVERT = 0xfd,            ///< halt execution, revert state and return output data
	INVALID = 0xfe,           ///< invalid instruction for expressing runtime errors (e.g., division-by-zero)
	SELFDESTRUCT = 0xff       ///< halt execution and register account for later deletion
};

/// @returns true if the instruction is of the CALL opcode family
constexpr bool isCallInstruction(Instruction _inst) noexcept
{
	switch (_inst)
	{
		case Instruction::CALL:
		case Instruction::CALLCODE:
		case Instruction::DELEGATECALL:
		case Instruction::STATICCALL:
		case Instruction::EXTCALL:
		case Instruction::EXTSTATICCALL:
		case Instruction::EXTDELEGATECALL:
			return true;
		default:
			return false;
	}
}

/// @returns true if the instruction is a PUSH
inline bool isPushInstruction(Instruction _inst)
{
	return Instruction::PUSH0 <= _inst && _inst <= Instruction::PUSH32;
}

/// @returns true if the instruction is a LOG
inline bool isLogInstruction(Instruction _inst)
{
	return Instruction::LOG0 <= _inst && _inst <= Instruction::LOG4;
}

/// @returns the number of PUSH Instruction _inst
inline unsigned getPushNumber(Instruction _inst)
{
	return static_cast<uint8_t>(_inst) - unsigned(Instruction::PUSH0);
}

/// @returns the number of LOG Instruction _inst
inline unsigned getLogNumber(Instruction _inst)
{
	return static_cast<uint8_t>(_inst) - unsigned(Instruction::LOG0);
}

/// @returns the PUSH<_number> instruction
inline Instruction pushInstruction(unsigned _number)
{
	solAssert(_number <= 32);
	return Instruction(unsigned(Instruction::PUSH0) + _number);
}

/// @returns the DUP<_number> instruction
inline Instruction dupInstruction(unsigned _number)
{
	solAssert(1 <= _number && _number <= 16);
	return Instruction(unsigned(Instruction::DUP1) + _number - 1);
}

/// @returns the SWAP<_number> instruction
inline Instruction swapInstruction(unsigned _number)
{
	solAssert(1 <= _number && _number <= 16);
	return Instruction(unsigned(Instruction::SWAP1) + _number - 1);
}

/// @returns the LOG<_number> instruction
inline Instruction logInstruction(unsigned _number)
{
	solAssert(_number <= 4);
	return Instruction(unsigned(Instruction::LOG0) + _number);
}

/// Gas price tiers representing static cost of an instruction.
/// Opcodes whose cost is dynamic or depends on EVM version should use the `Special` tier and need
/// dedicated logic in GasMeter (especially in estimateMax()).
/// The tiers loosely follow opcode groups originally defined in the Yellow Paper.
enum class Tier
{
	// NOTE: Tiers should be ordered by cost, since we sometimes perform comparisons between them.
	Zero = 0,   // 0, Zero
	Base,       // 2, Quick
	RJump,      // 2, RJump
	VeryLow,    // 3, Fastest
	RetF,       // 3,
	RJumpI,     // 4,
	Low,        // 5, Fast
	CallF,      // 5,
	JumpF,      // 5,
	Mid,        // 8, Mid
	High,       // 10, Slow
	BlockHash,  // 20
	WarmAccess, // 100, Warm Access
	Special,    // multiparam or otherwise special
	Invalid,    // Invalid.
};

/// Information structure for a particular instruction.
struct InstructionInfo
{
	std::string_view name; ///< The name of the instruction.
	int additional;        ///< Additional items required in memory for this instructions (only for PUSH).
	int args;              ///< Number of items required on the stack for this instruction (and, for the purposes of ret, the number taken from the stack).
	int ret;               ///< Number of items placed (back) on the stack by this instruction, assuming args items were removed.
	bool sideEffects;      ///< false if the only effect on the execution environment (apart from gas usage) is a change to a topmost segment of the stack
	Tier gasPriceTier;     ///< Tier for gas pricing.
};

namespace detail {
consteval std::array<InstructionInfo, 256> buildInstructionInfoTable()
{
	std::array<InstructionInfo, 256> t{};
	auto set = [&](Instruction i, std::string_view n, int a, int ar, int r, bool se, Tier g) {
		t[static_cast<uint8_t>(i)] = {n, a, ar, r, se, g};
	};
	//                                                        Add Args Ret SideEffects GasPriceTier
	set(Instruction::STOP,           "STOP",            0,  0,   0,  true,       Tier::Zero);
	set(Instruction::ADD,            "ADD",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SUB,            "SUB",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::MUL,            "MUL",             0,  2,   1,  false,      Tier::Low);
	set(Instruction::DIV,            "DIV",             0,  2,   1,  false,      Tier::Low);
	set(Instruction::SDIV,           "SDIV",            0,  2,   1,  false,      Tier::Low);
	set(Instruction::MOD,            "MOD",             0,  2,   1,  false,      Tier::Low);
	set(Instruction::SMOD,           "SMOD",            0,  2,   1,  false,      Tier::Low);
	set(Instruction::EXP,            "EXP",             0,  2,   1,  false,      Tier::Special);
	set(Instruction::NOT,            "NOT",             0,  1,   1,  false,      Tier::VeryLow);
	set(Instruction::LT,             "LT",              0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::GT,             "GT",              0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SLT,            "SLT",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SGT,            "SGT",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::EQ,             "EQ",              0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::ISZERO,         "ISZERO",          0,  1,   1,  false,      Tier::VeryLow);
	set(Instruction::AND,            "AND",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::OR,             "OR",              0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::XOR,            "XOR",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::BYTE,           "BYTE",            0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SHL,            "SHL",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SHR,            "SHR",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::SAR,            "SAR",             0,  2,   1,  false,      Tier::VeryLow);
	set(Instruction::CLZ,            "CLZ",             0,  1,   1,  false,      Tier::Low);
	set(Instruction::ADDMOD,         "ADDMOD",          0,  3,   1,  false,      Tier::Mid);
	set(Instruction::MULMOD,         "MULMOD",          0,  3,   1,  false,      Tier::Mid);
	set(Instruction::SIGNEXTEND,     "SIGNEXTEND",      0,  2,   1,  false,      Tier::Low);
	set(Instruction::KECCAK256,      "KECCAK256",       0,  2,   1,  true,       Tier::Special);
	set(Instruction::ADDRESS,        "ADDRESS",         0,  0,   1,  false,      Tier::Base);
	set(Instruction::BALANCE,        "BALANCE",         0,  1,   1,  false,      Tier::Special);
	set(Instruction::ORIGIN,         "ORIGIN",          0,  0,   1,  false,      Tier::Base);
	set(Instruction::CALLER,         "CALLER",          0,  0,   1,  false,      Tier::Base);
	set(Instruction::CALLVALUE,      "CALLVALUE",       0,  0,   1,  false,      Tier::Base);
	set(Instruction::CALLDATALOAD,   "CALLDATALOAD",    0,  1,   1,  false,      Tier::VeryLow);
	set(Instruction::CALLDATASIZE,   "CALLDATASIZE",    0,  0,   1,  false,      Tier::Base);
	set(Instruction::CALLDATACOPY,   "CALLDATACOPY",    0,  3,   0,  true,       Tier::VeryLow);
	set(Instruction::CODESIZE,       "CODESIZE",        0,  0,   1,  false,      Tier::Base);
	set(Instruction::CODECOPY,       "CODECOPY",        0,  3,   0,  true,       Tier::VeryLow);
	set(Instruction::GASPRICE,       "GASPRICE",        0,  0,   1,  false,      Tier::Base);
	set(Instruction::EXTCODESIZE,    "EXTCODESIZE",     0,  1,   1,  false,      Tier::Special);
	set(Instruction::EXTCODECOPY,    "EXTCODECOPY",     0,  4,   0,  true,       Tier::Special);
	set(Instruction::RETURNDATASIZE, "RETURNDATASIZE",  0,  0,   1,  false,      Tier::Base);
	set(Instruction::RETURNDATACOPY, "RETURNDATACOPY",  0,  3,   0,  true,       Tier::VeryLow);
	set(Instruction::MCOPY,          "MCOPY",           0,  3,   0,  true,       Tier::VeryLow);
	set(Instruction::EXTCODEHASH,    "EXTCODEHASH",     0,  1,   1,  false,      Tier::Special);
	set(Instruction::BLOCKHASH,      "BLOCKHASH",       0,  1,   1,  false,      Tier::BlockHash);
	set(Instruction::BLOBHASH,       "BLOBHASH",        0,  1,   1,  false,      Tier::VeryLow);
	set(Instruction::COINBASE,       "COINBASE",        0,  0,   1,  false,      Tier::Base);
	set(Instruction::TIMESTAMP,      "TIMESTAMP",       0,  0,   1,  false,      Tier::Base);
	set(Instruction::NUMBER,         "NUMBER",          0,  0,   1,  false,      Tier::Base);
	set(Instruction::PREVRANDAO,     "PREVRANDAO",      0,  0,   1,  false,      Tier::Base);
	set(Instruction::GASLIMIT,       "GASLIMIT",        0,  0,   1,  false,      Tier::Base);
	set(Instruction::CHAINID,        "CHAINID",         0,  0,   1,  false,      Tier::Base);
	set(Instruction::SELFBALANCE,    "SELFBALANCE",     0,  0,   1,  false,      Tier::Low);
	set(Instruction::BASEFEE,        "BASEFEE",         0,  0,   1,  false,      Tier::Base);
	set(Instruction::BLOBBASEFEE,    "BLOBBASEFEE",     0,  0,   1,  false,      Tier::Base);
	set(Instruction::POP,            "POP",             0,  1,   0,  false,      Tier::Base);
	set(Instruction::MLOAD,          "MLOAD",           0,  1,   1,  true,       Tier::VeryLow);
	set(Instruction::MSTORE,         "MSTORE",          0,  2,   0,  true,       Tier::VeryLow);
	set(Instruction::MSTORE8,        "MSTORE8",         0,  2,   0,  true,       Tier::VeryLow);
	set(Instruction::SLOAD,          "SLOAD",           0,  1,   1,  false,      Tier::Special);
	set(Instruction::SSTORE,         "SSTORE",          0,  2,   0,  true,       Tier::Special);
	set(Instruction::TLOAD,          "TLOAD",           0,  1,   1,  false,      Tier::WarmAccess);
	set(Instruction::TSTORE,         "TSTORE",          0,  2,   0,  true,       Tier::WarmAccess);
	set(Instruction::JUMP,           "JUMP",            0,  1,   0,  true,       Tier::Mid);
	set(Instruction::JUMPI,          "JUMPI",           0,  2,   0,  true,       Tier::High);
	set(Instruction::PC,             "PC",              0,  0,   1,  false,      Tier::Base);
	set(Instruction::MSIZE,          "MSIZE",           0,  0,   1,  false,      Tier::Base);
	set(Instruction::GAS,            "GAS",             0,  0,   1,  false,      Tier::Base);
	set(Instruction::JUMPDEST,       "JUMPDEST",        0,  0,   0,  true,       Tier::Special);
	set(Instruction::DATALOADN,      "DATALOADN",       2,  0,   1,  true,       Tier::Low);
	set(Instruction::RJUMP,          "RJUMP",           2,  0,   0,  true,       Tier::RJump);
	set(Instruction::RJUMPI,         "RJUMPI",          2,  1,   0,  true,       Tier::RJumpI);
	set(Instruction::PUSH0,          "PUSH0",           0,  0,   1,  false,      Tier::Base);
	set(Instruction::PUSH1,          "PUSH1",           1,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH2,          "PUSH2",           2,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH3,          "PUSH3",           3,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH4,          "PUSH4",           4,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH5,          "PUSH5",           5,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH6,          "PUSH6",           6,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH7,          "PUSH7",           7,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH8,          "PUSH8",           8,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH9,          "PUSH9",           9,  0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH10,         "PUSH10",          10, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH11,         "PUSH11",          11, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH12,         "PUSH12",          12, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH13,         "PUSH13",          13, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH14,         "PUSH14",          14, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH15,         "PUSH15",          15, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH16,         "PUSH16",          16, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH17,         "PUSH17",          17, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH18,         "PUSH18",          18, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH19,         "PUSH19",          19, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH20,         "PUSH20",          20, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH21,         "PUSH21",          21, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH22,         "PUSH22",          22, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH23,         "PUSH23",          23, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH24,         "PUSH24",          24, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH25,         "PUSH25",          25, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH26,         "PUSH26",          26, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH27,         "PUSH27",          27, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH28,         "PUSH28",          28, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH29,         "PUSH29",          29, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH30,         "PUSH30",          30, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH31,         "PUSH31",          31, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::PUSH32,         "PUSH32",          32, 0,   1,  false,      Tier::VeryLow);
	set(Instruction::DUP1,           "DUP1",            0,  1,   2,  false,      Tier::VeryLow);
	set(Instruction::DUP2,           "DUP2",            0,  2,   3,  false,      Tier::VeryLow);
	set(Instruction::DUP3,           "DUP3",            0,  3,   4,  false,      Tier::VeryLow);
	set(Instruction::DUP4,           "DUP4",            0,  4,   5,  false,      Tier::VeryLow);
	set(Instruction::DUP5,           "DUP5",            0,  5,   6,  false,      Tier::VeryLow);
	set(Instruction::DUP6,           "DUP6",            0,  6,   7,  false,      Tier::VeryLow);
	set(Instruction::DUP7,           "DUP7",            0,  7,   8,  false,      Tier::VeryLow);
	set(Instruction::DUP8,           "DUP8",            0,  8,   9,  false,      Tier::VeryLow);
	set(Instruction::DUP9,           "DUP9",            0,  9,   10, false,      Tier::VeryLow);
	set(Instruction::DUP10,          "DUP10",           0,  10,  11, false,      Tier::VeryLow);
	set(Instruction::DUP11,          "DUP11",           0,  11,  12, false,      Tier::VeryLow);
	set(Instruction::DUP12,          "DUP12",           0,  12,  13, false,      Tier::VeryLow);
	set(Instruction::DUP13,          "DUP13",           0,  13,  14, false,      Tier::VeryLow);
	set(Instruction::DUP14,          "DUP14",           0,  14,  15, false,      Tier::VeryLow);
	set(Instruction::DUP15,          "DUP15",           0,  15,  16, false,      Tier::VeryLow);
	set(Instruction::DUP16,          "DUP16",           0,  16,  17, false,      Tier::VeryLow);
	set(Instruction::SWAP1,          "SWAP1",           0,  2,   2,  false,      Tier::VeryLow);
	set(Instruction::SWAP2,          "SWAP2",           0,  3,   3,  false,      Tier::VeryLow);
	set(Instruction::SWAP3,          "SWAP3",           0,  4,   4,  false,      Tier::VeryLow);
	set(Instruction::SWAP4,          "SWAP4",           0,  5,   5,  false,      Tier::VeryLow);
	set(Instruction::SWAP5,          "SWAP5",           0,  6,   6,  false,      Tier::VeryLow);
	set(Instruction::SWAP6,          "SWAP6",           0,  7,   7,  false,      Tier::VeryLow);
	set(Instruction::SWAP7,          "SWAP7",           0,  8,   8,  false,      Tier::VeryLow);
	set(Instruction::SWAP8,          "SWAP8",           0,  9,   9,  false,      Tier::VeryLow);
	set(Instruction::SWAP9,          "SWAP9",           0,  10,  10, false,      Tier::VeryLow);
	set(Instruction::SWAP10,         "SWAP10",          0,  11,  11, false,      Tier::VeryLow);
	set(Instruction::SWAP11,         "SWAP11",          0,  12,  12, false,      Tier::VeryLow);
	set(Instruction::SWAP12,         "SWAP12",          0,  13,  13, false,      Tier::VeryLow);
	set(Instruction::SWAP13,         "SWAP13",          0,  14,  14, false,      Tier::VeryLow);
	set(Instruction::SWAP14,         "SWAP14",          0,  15,  15, false,      Tier::VeryLow);
	set(Instruction::SWAP15,         "SWAP15",          0,  16,  16, false,      Tier::VeryLow);
	set(Instruction::SWAP16,         "SWAP16",          0,  17,  17, false,      Tier::VeryLow);
	set(Instruction::LOG0,           "LOG0",            0,  2,   0,  true,       Tier::Special);
	set(Instruction::LOG1,           "LOG1",            0,  3,   0,  true,       Tier::Special);
	set(Instruction::LOG2,           "LOG2",            0,  4,   0,  true,       Tier::Special);
	set(Instruction::LOG3,           "LOG3",            0,  5,   0,  true,       Tier::Special);
	set(Instruction::LOG4,           "LOG4",            0,  6,   0,  true,       Tier::Special);
	set(Instruction::RETF,           "RETF",            0,  0,   0,  true,       Tier::RetF);
	set(Instruction::CALLF,          "CALLF",           2,  0,   0,  true,       Tier::CallF);
	set(Instruction::JUMPF,          "JUMPF",           2,  0,   0,  true,       Tier::JumpF);
	set(Instruction::SWAPN,          "SWAPN",           1,  0,   0,  false,      Tier::VeryLow);
	set(Instruction::DUPN,           "DUPN",            1,  0,   0,  false,      Tier::VeryLow);
	set(Instruction::EOFCREATE,      "EOFCREATE",       1,  4,   1,  true,       Tier::Special);
	set(Instruction::RETURNCONTRACT, "RETURNCONTRACT",  1,  2,   0,  true,       Tier::Special);
	set(Instruction::CREATE,         "CREATE",          0,  3,   1,  true,       Tier::Special);
	set(Instruction::CALL,           "CALL",            0,  7,   1,  true,       Tier::Special);
	set(Instruction::CALLCODE,       "CALLCODE",        0,  7,   1,  true,       Tier::Special);
	set(Instruction::RETURN,         "RETURN",          0,  2,   0,  true,       Tier::Zero);
	set(Instruction::DELEGATECALL,   "DELEGATECALL",    0,  6,   1,  true,       Tier::Special);
	set(Instruction::STATICCALL,     "STATICCALL",      0,  6,   1,  true,       Tier::Special);
	set(Instruction::EXTCALL,        "EXTCALL",         0,  4,   1,  true,       Tier::Special);
	set(Instruction::EXTDELEGATECALL,"EXTDELEGATECALL", 0,  3,   1,  true,       Tier::Special);
	set(Instruction::EXTSTATICCALL,  "EXTSTATICCALL",   0,  3,   1,  true,       Tier::Special);
	set(Instruction::CREATE2,        "CREATE2",         0,  4,   1,  true,       Tier::Special);
	set(Instruction::REVERT,         "REVERT",          0,  2,   0,  true,       Tier::Zero);
	set(Instruction::INVALID,        "INVALID",         0,  0,   0,  true,       Tier::Zero);
	set(Instruction::SELFDESTRUCT,   "SELFDESTRUCT",    0,  1,   0,  true,       Tier::Special);
	return t;
}
} // namespace detail

inline constexpr std::array<InstructionInfo, 256> c_instructionInfo = detail::buildInstructionInfoTable();

/// Information on all the instructions.
constexpr InstructionInfo instructionInfo(Instruction _inst, langutil::EVMVersion _evmVersion)
{
	if (_inst == Instruction::PREVRANDAO && _evmVersion < langutil::EVMVersion::paris())
		return {"DIFFICULTY", 0, 0, 1, false, Tier::Base};
	return c_instructionInfo[static_cast<uint8_t>(_inst)];
}

/// Fast lookup — does not handle the DIFFICULTY alias (pre-Paris).
constexpr InstructionInfo instructionInfo(Instruction _inst)
{
	return c_instructionInfo[static_cast<uint8_t>(_inst)];
}

/// check whether instructions exists.
constexpr bool isValidInstruction(Instruction _inst)
{
	return !c_instructionInfo[static_cast<uint8_t>(_inst)].name.empty();
}

/// O(log n) lookup via sorted array: string mnemonic -> Instruction. Includes "DIFFICULTY" alias.
std::optional<Instruction> instructionByName(std::string_view _name);

/// All (name, instruction) pairs for iteration. Includes "DIFFICULTY" alias.
std::span<std::pair<std::string_view, Instruction> const> allNamedInstructions();

}
