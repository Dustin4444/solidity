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
 * Control flow graph and stack layout structures used during code generation.
 */

#pragma once

#include <libyul/backends/evm/ssa/SSACFGDebugInfo.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <libyul/backends/evm/EVMDialect.h>

#include <libyul/AST.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/Dialect.h>
#include <libyul/Exceptions.h>

#include <libsolutil/Numeric.h>

#include <concepts>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace solidity::yul::ssa
{
class LivenessAnalysis;
struct ControlFlowGraphs;

class SSACFG
{
public:
	using DebugInfo = SSACFGDebugInfo;

	explicit SSACFG(
		EVMDialect const& _evmVersion,
		std::unique_ptr<DebugInfo> _debugInfo = nullptr
	):
		evmDialect(_evmVersion),
		debugInfo(std::move(_debugInfo))
	{}

	SSACFG(SSACFG const&) = delete;
	SSACFG(SSACFG&&) = delete;
	SSACFG& operator=(SSACFG const&) = delete;
	SSACFG& operator=(SSACFG&&) = delete;
	~SSACFG() = default;

	using BlockId = ssa::BlockId;
	using InstId = ssa::InstId;
	using ValueId = ssa::ValueId;
	using Opcode = ssa::Opcode;

	struct BuiltinCall
	{
		std::reference_wrapper<BuiltinFunction const> builtin;
		std::reference_wrapper<FunctionCall const> call;
	};
	struct Call
	{
		FunctionGraphID graphID;
		std::reference_wrapper<FunctionCall const> call;
		bool canContinue;
	};

	/// Uniform node for the SSA CFG — every instruction category lives in a single pool.
	/// See uniformity_of_ssa_cfg_v2.md for the design.
	struct Inst
	{
		Opcode opcode;
		uint32_t payloadIndex = std::numeric_limits<uint32_t>::max();
		BlockId block{};
		std::vector<ValueId> inputs{};
		std::vector<ValueId> outputs{};
	};

	struct BasicBlock
	{
		struct MainExit {};
		struct ConditionalJump
		{
			ValueId condition;
			BlockId nonZero;
			BlockId zero;
		};
		struct Jump
		{
			BlockId target;
		};
		struct FunctionReturn
		{
			std::vector<ValueId> returnValues;
		};
		struct Terminated {};
		std::vector<BlockId> entries;
		/// Flat per-block instruction list in program order. Consumers iterate this
		/// and filter by Opcode to find phis, operations, or upsilons.
		std::vector<InstId> instructions;
		std::variant<MainExit, Jump, ConditionalJump, FunctionReturn, Terminated> exit = MainExit{};

		template<std::invocable<BlockId> Callable>
		void forEachExit(Callable&& _callable) const
		{
			if (auto* jump = std::get_if<Jump>(&exit))
				_callable(jump->target);
			else if (auto* conditionalJump = std::get_if<ConditionalJump>(&exit))
			{
				_callable(conditionalJump->nonZero);
				_callable(conditionalJump->zero);
			}
		}

		bool isMainExitBlock() const { return std::holds_alternative<MainExit>(exit); }
		bool isTerminationBlock() const { return std::holds_alternative<Terminated>(exit); }
		bool isFunctionReturnBlock() const { return std::holds_alternative<FunctionReturn>(exit); }
		bool isJumpBlock() const { return std::holds_alternative<Jump>(exit); }
	};

	BlockId makeBlock(langutil::DebugData::ConstPtr _debugData)
	{
		BlockId blockId{static_cast<BlockId::ValueType>(m_blocks.size())};
		m_blocks.emplace_back(BasicBlock{{}, {}, BasicBlock::Terminated{}});
		if (debugInfo)
			debugInfo->setBlockDebugData(blockId, std::move(_debugData));
		return blockId;
	}
	BasicBlock& block(BlockId _id) { return m_blocks.at(_id.value); }
	BasicBlock const& block(BlockId _id) const { return m_blocks.at(_id.value); }
	size_t numBlocks() const { return m_blocks.size(); }

	/// Accessors for the Inst pool.
	Inst& inst(InstId _id) { return m_insts.at(_id.value); }
	Inst const& inst(InstId _id) const { return m_insts.at(_id.value); }
	size_t numInsts() const { return m_insts.size(); }
	/// Returns the opcode category for a given ValueId.
	Opcode kindOf(ValueId _v) const { return m_insts.at(_v.instIdx()).opcode; }

	/// Returns the phi targeted by an Upsilon Inst.
	ValueId upsilonPhi(InstId _id) const
	{
		yulAssert(m_insts.at(_id.value).opcode == Opcode::Upsilon);
		return m_upsilonPhis.at(m_insts.at(_id.value).payloadIndex);
	}

	/// Returns the u256 payload of a Const Inst.
	u256 const& literalPayload(InstId _id) const
	{
		yulAssert(m_insts.at(_id.value).opcode == Opcode::Const);
		return m_literalPayloads.at(m_insts.at(_id.value).payloadIndex);
	}

	BuiltinCall const& builtinPayload(InstId _id) const
	{
		yulAssert(m_insts.at(_id.value).opcode == Opcode::BuiltinCall);
		return m_builtinPayloads.at(m_insts.at(_id.value).payloadIndex);
	}

	Call const& callPayload(InstId _id) const
	{
		yulAssert(m_insts.at(_id.value).opcode == Opcode::Call);
		return m_callPayloads.at(m_insts.at(_id.value).payloadIndex);
	}

	/// Creates a Phi Inst in the given block and returns its output ValueId.
	ValueId newPhi(BlockId _definingBlock)
	{
		InstId const id = appendInst(Inst{Opcode::Phi, 0, _definingBlock, {}, {}});
		ValueId const v = ValueId::makeOutput(id, 0, Opcode::Phi);
		m_insts.at(id.value).outputs.push_back(v);
		m_blocks.at(_definingBlock.value).instructions.push_back(id);
		if (debugInfo)
			debugInfo->setValueDebugData(v, debugInfo->blockDebugData(_definingBlock));
		return v;
	}

	/// Creates a FunctionArg Inst for a function parameter and returns its output ValueId.
	/// Function arguments are uniform with other ValueIds in the pool — they are outputs
	/// of a 0-input, 1-output Inst in the entry block's instruction list.
	ValueId newFunctionArgument(BlockId _entryBlock)
	{
		InstId const id = appendInst(Inst{Opcode::FunctionArg, 0, _entryBlock, {}, {}});
		ValueId const v = ValueId::makeOutput(id, 0, Opcode::FunctionArg);
		m_insts.at(id.value).outputs.push_back(v);
		m_blocks.at(_entryBlock.value).instructions.push_back(id);
		if (debugInfo)
			debugInfo->setValueDebugData(v, debugInfo->blockDebugData(_entryBlock));
		return v;
	}

	/// Creates an Unreachable Inst and returns its output ValueId. Per-use: every call
	/// produces a fresh Inst. No singleton semantics; equality between two Unreachable
	/// ValueIds is not meaningful — use kindOf(v) == Opcode::Unreachable instead.
	ValueId unreachableValue()
	{
		InstId const id = appendInst(Inst{Opcode::Unreachable, std::numeric_limits<uint32_t>::max(), BlockId{}, {}, {}});
		ValueId const v = ValueId::makeOutput(id, 0, Opcode::Unreachable);
		m_insts.at(id.value).outputs.push_back(v);
		return v;
	}

	/// Creates (or looks up) a Const Inst with the given u256 value.
	/// Literal ValueIds are deduplicated.
	ValueId newLiteral(langutil::DebugData::ConstPtr _debugData, u256 _value)
	{
		auto const it = m_literalDedup.find(_value);
		if (it != m_literalDedup.end())
			return ValueId::makeOutput(it->second, 0, Opcode::Const);

		uint32_t const payloadIdx = static_cast<uint32_t>(m_literalPayloads.size());
		m_literalPayloads.push_back(_value);
		InstId const id = appendInst(Inst{Opcode::Const, payloadIdx, BlockId{}, {}, {}});
		ValueId const v = ValueId::makeOutput(id, 0, Opcode::Const);
		m_insts.at(id.value).outputs.push_back(v);
		m_literalDedup.emplace(std::move(_value), id);
		if (debugInfo)
			debugInfo->setValueDebugData(v, std::move(_debugData));
		return v;
	}

	/// Creates a BuiltinCall Inst. Returns its output ValueIds (one per return value).
	std::vector<ValueId> makeBuiltinCall(
		BlockId _block,
		BuiltinCall _payload,
		std::vector<ValueId> _inputs,
		std::size_t _numOutputs,
		langutil::DebugData::ConstPtr _debugData = {}
	)
	{
		yulAssert(_block.hasValue());
		uint32_t const payloadIdx = static_cast<uint32_t>(m_builtinPayloads.size());
		m_builtinPayloads.push_back(std::move(_payload));
		InstId const id = appendInst(Inst{Opcode::BuiltinCall, payloadIdx, _block, std::move(_inputs), {}});
		std::vector<ValueId> outputs;
		outputs.reserve(_numOutputs);
		for (std::size_t i = 0; i < _numOutputs; ++i)
			outputs.push_back(ValueId::makeOutput(id, static_cast<ValueId::OutputPos>(i), Opcode::BuiltinCall));
		m_insts.at(id.value).outputs = outputs;
		m_blocks.at(_block.value).instructions.push_back(id);
		if (debugInfo && _debugData)
			debugInfo->setInstDebugData(id, std::move(_debugData));
		return outputs;
	}

	/// Creates a Call Inst. Returns its output ValueIds (one per return value).
	std::vector<ValueId> makeCall(
		BlockId _block,
		Call _payload,
		std::vector<ValueId> _inputs,
		std::size_t _numOutputs,
		langutil::DebugData::ConstPtr _debugData = {}
	)
	{
		yulAssert(_block.hasValue());
		uint32_t const payloadIdx = static_cast<uint32_t>(m_callPayloads.size());
		m_callPayloads.push_back(std::move(_payload));
		InstId const id = appendInst(Inst{Opcode::Call, payloadIdx, _block, std::move(_inputs), {}});
		std::vector<ValueId> outputs;
		outputs.reserve(_numOutputs);
		for (std::size_t i = 0; i < _numOutputs; ++i)
			outputs.push_back(ValueId::makeOutput(id, static_cast<ValueId::OutputPos>(i), Opcode::Call));
		m_insts.at(id.value).outputs = outputs;
		m_blocks.at(_block.value).instructions.push_back(id);
		if (debugInfo && _debugData)
			debugInfo->setInstDebugData(id, std::move(_debugData));
		return outputs;
	}

	/// Appends an Upsilon Inst to the given block.
	InstId emitUpsilon(BlockId _block, ValueId _value, ValueId _phi)
	{
		yulAssert(kindOf(_phi) == Opcode::Phi);
		uint32_t const payloadIdx = static_cast<uint32_t>(m_upsilonPhis.size());
		m_upsilonPhis.push_back(_phi);
		InstId const id = appendInst(Inst{Opcode::Upsilon, payloadIdx, _block, {_value}, {}});
		m_blocks.at(_block.value).instructions.push_back(id);
		return id;
	}

	std::string toDot(
		bool _includeDiGraphDefinition=true,
		std::optional<size_t> _functionIndex=std::nullopt,
		LivenessAnalysis const* _liveness=nullptr,
		ControlFlowGraphs const* _controlFlow=nullptr
	) const;

private:
	InstId appendInst(Inst _inst)
	{
		InstId const id{static_cast<InstId::ValueType>(m_insts.size())};
		m_insts.emplace_back(std::move(_inst));
		return id;
	}

	std::vector<BasicBlock> m_blocks;
	std::vector<Inst> m_insts;
	std::vector<u256> m_literalPayloads;
	std::map<u256, InstId> m_literalDedup;
	std::vector<ValueId> m_upsilonPhis;
	std::vector<BuiltinCall> m_builtinPayloads;
	std::vector<Call> m_callPayloads;
public:
	EVMDialect const& evmDialect;
	std::unique_ptr<DebugInfo> debugInfo;
	BlockId entry = BlockId{0};
	std::set<BlockId> exits;
	std::string name{};
	bool canContinue = true;
	std::vector<ValueId> arguments;
	std::size_t numReturns = 0;
	// Container for artificial calls generated for switch statements.
	std::list<FunctionCall> ghostCalls;

	bool isMainGraph() const { return name.empty(); }
};

}
