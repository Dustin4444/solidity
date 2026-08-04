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

#include <libyul/backends/evm/ssa/transform/ConstantFolder.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/Exceptions.h>

#include <libevmasm/RuleList.h>

#include <map>
#include <optional>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

/// Evaluates a pure EVM instruction on constant arguments. Semantics mirror the constant-folding
/// rules in libevmasm/RuleList.h. Returns nullopt for instructions that are not pure functions of
/// their arguments (which also serves as the whitelist of foldable operations).
std::optional<u256> evaluateInstruction(evmasm::Instruction const _instruction, std::vector<u256> const& _args)
{
	using evmasm::Instruction;
	using evmasm::divWorkaround;
	using evmasm::modWorkaround;
	using evmasm::shlWorkaround;
	auto const arg = [&](std::size_t const _index) -> u256 const& { return _args.at(_index); };
	switch (_instruction)
	{
	case Instruction::ADD: return arg(0) + arg(1);
	case Instruction::MUL: return arg(0) * arg(1);
	case Instruction::SUB: return arg(0) - arg(1);
	case Instruction::DIV: return arg(1) == 0 ? u256(0) : divWorkaround(arg(0), arg(1));
	case Instruction::SDIV: return arg(1) == 0 ? u256(0) : s2u(divWorkaround(u2s(arg(0)), u2s(arg(1))));
	case Instruction::MOD: return arg(1) == 0 ? u256(0) : modWorkaround(arg(0), arg(1));
	case Instruction::SMOD: return arg(1) == 0 ? u256(0) : s2u(modWorkaround(u2s(arg(0)), u2s(arg(1))));
	case Instruction::EXP: return u256(boost::multiprecision::powm(bigint(arg(0)), bigint(arg(1)), bigint(1) << 256));
	case Instruction::NOT: return ~arg(0);
	case Instruction::LT: return arg(0) < arg(1) ? 1 : 0;
	case Instruction::GT: return arg(0) > arg(1) ? 1 : 0;
	case Instruction::SLT: return u2s(arg(0)) < u2s(arg(1)) ? 1 : 0;
	case Instruction::SGT: return u2s(arg(0)) > u2s(arg(1)) ? 1 : 0;
	case Instruction::EQ: return arg(0) == arg(1) ? 1 : 0;
	case Instruction::ISZERO: return arg(0) == 0 ? 1 : 0;
	case Instruction::AND: return arg(0) & arg(1);
	case Instruction::OR: return arg(0) | arg(1);
	case Instruction::XOR: return arg(0) ^ arg(1);
	case Instruction::BYTE: return arg(0) >= 32 ? u256(0) : (arg(1) >> unsigned(8 * (31 - arg(0)))) & 0xff;
	case Instruction::ADDMOD: return arg(2) == 0 ? u256(0) : u256((bigint(arg(0)) + bigint(arg(1))) % arg(2));
	case Instruction::MULMOD: return arg(2) == 0 ? u256(0) : u256((bigint(arg(0)) * bigint(arg(1))) % arg(2));
	case Instruction::SIGNEXTEND:
	{
		if (arg(0) >= 31)
			return arg(1);
		unsigned const testBit = unsigned(arg(0)) * 8 + 7;
		u256 const mask = (u256(1) << testBit) - 1;
		return boost::multiprecision::bit_test(arg(1), testBit) ? arg(1) | ~mask : arg(1) & mask;
	}
	case Instruction::SHL: return arg(0) >= 256 ? u256(0) : shlWorkaround(arg(1), unsigned(arg(0)));
	case Instruction::SHR: return arg(0) >= 256 ? u256(0) : arg(1) >> unsigned(arg(0));
	case Instruction::SAR:
	{
		if (arg(0) >= 256)
			return boost::multiprecision::bit_test(arg(1), 255) ? ~u256(0) : u256(0);
		u256 const shifted = arg(1) >> unsigned(arg(0));
		if (!boost::multiprecision::bit_test(arg(1), 255))
			return shifted;
		return shifted | ~(~u256(0) >> unsigned(arg(0)));
	}
	default: return std::nullopt;
	}
}

/// Evaluates `_id` to a compile-time constant if it is a literal or a tree of pure builtin calls
/// over literals, otherwise returns nullopt. Memoized in `_memo` (keyed by identity-resolved ids)
/// to stay linear on shared subtrees.
std::optional<u256> tryEvaluate(SSACFG const& _cfg, std::map<InstId, std::optional<u256>>& _memo, InstId const _id)
{
	InstId const id = _cfg.resolveIdentity(_id);
	if (auto const it = _memo.find(id); it != _memo.end())
		return it->second;

	std::optional<u256> result;
	if (_cfg.isLiteral(id))
		result = _cfg.literalPayload(id);
	else if (_cfg.kindOf(id) == InstOpcode::BuiltinCall)
	{
		auto const& builtin = _cfg.evmDialect.builtin(_cfg.builtinPayload(id).builtin);
		if (builtin.instruction && builtin.numReturns == 1)
		{
			std::vector<u256> args;
			auto const& inputs = _cfg.inst(id).inputs;
			args.reserve(inputs.size());
			for (InstId const input: inputs)
				if (std::optional<u256> const value = tryEvaluate(_cfg, _memo, input))
					args.push_back(*value);
				else
					break;
			if (args.size() == inputs.size())
				result = evaluateInstruction(*builtin.instruction, args);
		}
	}
	_memo.emplace(id, result);
	return result;
}

}

bool transform::foldConstants(SSACFG& _cfg)
{
	std::map<InstId, std::optional<u256>> memo;
	bool foldedAny = false;
	for (BlockId const blockId: _cfg.liveBlocks())
	{
		auto& block = _cfg.block(blockId);
		// Index loop: `newLiteral` appends newly deduplicated Const insts to the entry block's
		// instruction list while we iterate it. The appended insts are Consts and get skipped.
		for (std::size_t i = 0; i < block.instructions.size(); ++i)
		{
			InstId const instId = block.instructions[i];
			if (_cfg.kindOf(instId) != InstOpcode::BuiltinCall)
				continue;
			if (std::optional<u256> const value = tryEvaluate(_cfg, memo, instId))
			{
				// Forward to an entry-pinned, deduplicated literal instead of morphing in place:
				// Const insts are only ever allocated in the entry block and liveness must never
				// see a freely-generatable value. The Identity is cleaned up by the pipeline's
				// IdentityAndNopRemover.
				_cfg.replaceWithIdentity(instId, _cfg.newLiteral({}, *value));
				foldedAny = true;
			}
		}
	}
	return foldedAny;
}
