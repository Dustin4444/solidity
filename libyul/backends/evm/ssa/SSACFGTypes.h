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
/**
 * Lightweight ID types used throughout the SSA CFG.
 */

#pragma once

#include <libsolutil/Assertions.h>

#include <fmt/format.h>

#include <cstdint>
#include <limits>
#include <string>

namespace solidity::yul::ssa
{

class SSACFG;

using FunctionGraphID = std::uint32_t;

struct BlockId
{
	using ValueType = std::uint32_t;
	ValueType value = std::numeric_limits<ValueType>::max();
	bool hasValue() const { return value != std::numeric_limits<ValueType>::max(); }
	auto operator<=>(BlockId const&) const = default;
};

struct InstId
{
	using ValueType = std::uint32_t;
	ValueType value = std::numeric_limits<ValueType>::max();
	bool hasValue() const { return value != std::numeric_limits<ValueType>::max(); }
	auto operator<=>(InstId const&) const = default;
};

/// Opcode tag for Inst. The enum covers all instruction categories in the uniform Inst pool.
/// See uniformity_of_ssa_cfg_v2.md for the full design.
enum class Opcode : std::uint8_t
{
	Const,        ///< literal u256 value; payload indexes m_literalPayloads
	Phi,          ///< reads from shadow variable; no inputs
	Upsilon,      ///< writes to shadow variable; inputs = {value}; payload indexes m_upsilonPhis
	BuiltinCall,  ///< EVM opcode / dialect builtin; payload indexes m_builtinPayloads
	Call,         ///< user-defined function call; payload indexes m_callPayloads
	Unreachable,  ///< per-use sentinel for dead-code paths; no payload
	FunctionArg,  ///< function parameter; no inputs, single output ValueId stored in cfg.arguments
	/// Forwards to a single replacement (inputs[0]) without producing its own output.
	/// Used by transformation passes to mark an Inst as "use my inputs[0] instead".
	/// The shared cleanup pass (transform::cleanup) path-compresses Identity chains
	/// and rewrites every ValueId operand (instruction inputs, terminator fields,
	/// upsilon phi payloads) to point at the terminal replacement, then removes
	/// Identity Insts from block.instructions.
	Identity,
};

/// Identifies a specific produced value of an Inst. Carries an opcode cache
/// for hot-path consumers (StackSlot inner loops, liveness filter); the
/// cache is set at construction from the defining Inst's opcode and is
/// immutable thereafter. Use cfg.kindOf(v) when the cache might be stale
/// (e.g. after Identity-rewriting passes).
/// Layout: 4-byte inst index, 1-byte output position, 1-byte opcode cache,
/// 2 bytes padding => 8 bytes.
class ValueId
{
public:
	using ValueType = std::uint32_t;
	using OutputPos = std::uint8_t;

	constexpr ValueId() = default;
	constexpr ValueId(InstId _instId, OutputPos _outputPos, Opcode _opcode): m_instIdx(_instId.value), m_outputPos(_outputPos), m_opcode(_opcode) {}
	constexpr ValueId(ValueId const&) = default;
	constexpr ValueId(ValueId&&) = default;
	constexpr ValueId& operator=(ValueId const&) = default;
	constexpr ValueId& operator=(ValueId&&) = default;

	static ValueId constexpr makeOutput(InstId _instId, OutputPos _pos, Opcode _opcode) { return ValueId{_instId, _pos, _opcode}; }

	bool constexpr hasValue() const { return m_instIdx != std::numeric_limits<ValueType>::max(); }
	ValueType constexpr instIdx() const noexcept { return m_instIdx; }
	OutputPos constexpr outputPos() const noexcept { return m_outputPos; }
	InstId constexpr instId() const noexcept { return InstId{m_instIdx}; }
	Opcode constexpr opcodeCache() const noexcept { return m_opcode; }

	/// Hot-path opcode checks using the local cache.
	bool constexpr isLiteral() const noexcept { return m_opcode == Opcode::Const; }
	bool constexpr isPhi() const noexcept { return m_opcode == Opcode::Phi; }
	bool constexpr isUnreachable() const noexcept { return m_opcode == Opcode::Unreachable; }
	/// Variable-like outputs: originate from BuiltinCall, Call, or FunctionArg.
	bool constexpr isVariable() const noexcept
	{
		return m_opcode == Opcode::BuiltinCall || m_opcode == Opcode::Call || m_opcode == Opcode::FunctionArg;
	}

	/// Returns a human-readable string representation. Uses the SSACFG to
	/// discriminate Const / Phi / Variable / Unreachable.
	std::string str(SSACFG const& _cfg) const;

	auto operator<=>(ValueId const&) const = default;

private:
	ValueType m_instIdx{std::numeric_limits<ValueType>::max()};
	OutputPos m_outputPos{0};
	Opcode m_opcode{Opcode::Unreachable};
};

static_assert(sizeof(ValueId) == 8, "ValueId layout regressed; cache efficiency relies on 8-byte ValueId");
static_assert(alignof(ValueId) == 4);

}

template<>
struct fmt::formatter<solidity::yul::ssa::BlockId>
{
	static auto constexpr parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

	template<typename FormatContext>
	auto format(solidity::yul::ssa::BlockId const& _blockId, FormatContext& _ctx) const -> decltype(_ctx.out())
	{
		if (!_blockId.hasValue())
			return fmt::format_to(_ctx.out(), "empty");
		return fmt::format_to(_ctx.out(), "{}", _blockId.value);
	}
};

template<>
struct fmt::formatter<solidity::yul::ssa::InstId>
{
	static auto constexpr parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

	template<typename FormatContext>
	auto format(solidity::yul::ssa::InstId const& _instId, FormatContext& _ctx) const -> decltype(_ctx.out())
	{
		if (!_instId.hasValue())
			return fmt::format_to(_ctx.out(), "empty");
		return fmt::format_to(_ctx.out(), "i{}", _instId.value);
	}
};

template<>
struct fmt::formatter<solidity::yul::ssa::ValueId>
{
	static auto constexpr parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

	template<typename FormatContext>
	auto format(solidity::yul::ssa::ValueId const& _valueId, FormatContext& _ctx) const -> decltype(_ctx.out())
	{
		using solidity::yul::ssa::Opcode;
		if (!_valueId.hasValue())
			return fmt::format_to(_ctx.out(), "empty");
		switch (_valueId.opcodeCache())
		{
		case Opcode::Const:
			return fmt::format_to(_ctx.out(), "lit{}", _valueId.instIdx());
		case Opcode::Phi:
			return fmt::format_to(_ctx.out(), "phi{}", _valueId.instIdx());
		case Opcode::Unreachable:
			return fmt::format_to(_ctx.out(), "unreachable");
		default:
			break;
		}
		if (_valueId.outputPos() == 0)
			return fmt::format_to(_ctx.out(), "v{}", _valueId.instIdx());
		return fmt::format_to(_ctx.out(), "v{}.{}", _valueId.instIdx(), _valueId.outputPos());
	}
};
