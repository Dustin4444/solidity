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

#pragma once

#include <libyul/backends/evm/ssa/PhiInverse.h>
#include <libyul/backends/evm/ssa/spill/Emitter.h>
#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackLayout.h>
#include <libyul/backends/evm/ssa/spill/MemoryAddressing.h>
#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libyul/backends/evm/AbstractAssembly.h>

#include <libevmasm/Instruction.h>

#include <libsolutil/Numeric.h>

#include <fmt/format.h>

#include <cstdlib>
#include <iostream>
#include <optional>

namespace solidity::yul
{
struct BuiltinContext;
}
namespace solidity::yul::ssa
{

struct AssemblyCallbacks
{
	void swap(StackDepth const _depth)
	{
		assembly->appendInstruction(evmasm::swapInstruction(static_cast<unsigned>(_depth.value)));
	}

	void pop()
	{
		assembly->appendInstruction(evmasm::Instruction::POP);
	}

	void push(StackSlot const& _slot)
	{
		switch (_slot.kind())
		{
		case StackSlot::Kind::Value:
		{
			auto const id = _slot.value();
			if (cfg->isLiteral(id))
			{
				assembly->appendConstant(cfg->literalPayload(id));
				return;
			}
			if (spillEmitter && spillSet->isSpilled(id))
			{
				spillEmitter->emitLoad(id);
				return;
			}
			yulAssert(false, fmt::format("Tried bringing up non-spilled non-const {}", id));
		}
		case StackSlot::Kind::Junk:
		{
			if (assembly->evmVersion().hasPush0())
				assembly->appendConstant(0);
			else
				assembly->appendInstruction(evmasm::Instruction::CODESIZE);
			return;
		}
		case StackSlot::Kind::FunctionCallReturnLabel:
		{
			auto const instId = callSites->instId(_slot.functionCallReturnLabel());
			yulAssert(returnLabels->count(instId), "FunctionCallReturnLabel not pre-registered before shuffle.");
			assembly->appendLabelReference(returnLabels->at(instId));
			return;
		}
		case StackSlot::Kind::FunctionReturnLabel:
		{
			yulAssert(false, "Cannot produce function return label.");
		}
		}
	}

	void dup(StackDepth const _depth)
	{
		assembly->appendInstruction(evmasm::dupInstruction(static_cast<unsigned>(_depth.value)));
	}

	SSACFG const* cfg{};
	AbstractAssembly* assembly{};
	CallSites const* callSites{};
	std::map<InstId, AbstractAssembly::LabelID> const* returnLabels{};
	/// Spill-set membership oracle, consulted before `emitLoad` to decide whether a value
	/// must be reloaded from memory. Always set; spilled values are loaded, others asserted on.
	spill::SpillSet const* spillSet{};
	/// Emitter for spilled-value MSTOREs and MLOADs. nullptr when this CFG has no spilling.
	spill::Emitter const* spillEmitter{};
};
static_assert(StackManipulationCallbackConcept<AssemblyCallbacks>);

/// Optional, test-only sink for the spill decisions `run` makes. When a non-null pointer is
/// passed to `run`, it is filled with one entry per CFG describing the final (post-cascade)
/// spill set and the memory address assigned to each spilled value. Production callers pass
/// `nullptr` and pay nothing; the contents never feed back into codegen.
struct SpillReport
{
	struct PerCFG
	{
		ControlFlowGraphs::FunctionGraphID graphID;
		bool isMainGraph = false;
		std::string functionName;
		spill::SpillSet spillSet;
		/// Spilled value -> reserved memory address, in `spilledValues()` order.
		std::vector<std::pair<InstId, u256>> addresses;
	};
	std::vector<PerCFG> perCFG;
};

class CodeTransform
{
public:
	static void run(
		AbstractAssembly& _assembly,
		ControlFlowGraphs& _controlFlowGraphs,
		ControlFlowGraphsLiveness const& _liveness,
		BuiltinContext& _builtinContext,
		SpillReport* _spillReport = nullptr
	);

private:
	using FunctionLabels = std::map<ControlFlowGraphs::FunctionGraphID, AbstractAssembly::LabelID>;

	static FunctionLabels registerFunctionLabels(
		AbstractAssembly& _assembly,
		ControlFlowGraphs const& _controlFlow
	);

	CodeTransform(
		AbstractAssembly& _assembly,
		BuiltinContext& _builtinContext,
		ControlFlowGraphs const& _controlFlow,
		FunctionLabels const& _functionLabels,
		CallSites const& _callSites,
		SSACFG const& _cfg,
		SSACFGStackLayout const& _stackLayout,
		spill::SpillSet const& _spillSet,
		ControlFlowGraphs::FunctionGraphID _graphID,
		spill::MemoryAddressing const& _addressing);

	void operator()(SSACFG::BlockId _blockId);
	void operator()(InstId _instId, StackData const& _operationInputLayout);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::MainExit const& _mainExit);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::ConditionalJump const& _conditionalJump);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::Jump const& _jump);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::FunctionReturn const& _functionReturn);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::Terminated const& _terminated);

	void prepareBlockExitStack(
		StackData const& _target,
		PhiInverse const& _phiInverse
	);

	AbstractAssembly& m_assembly;
	BuiltinContext& m_builtinContext;
	ControlFlowGraphs const& m_controlFlow;
	FunctionLabels const& m_functionLabels;
	CallSites const& m_callSites;
	SSACFG const& m_cfg;
	SSACFGStackLayout const& m_stackLayout;
	spill::SpillSet const& m_spillSet;
	ControlFlowGraphs::FunctionGraphID const m_graphID;

	std::vector<std::uint8_t> m_blockIsTransformed;
	std::vector<AbstractAssembly::LabelID> m_blockLabels;
	/// Constructed only when this CFG has any spilled values; otherwise nullopt.
	std::optional<spill::Emitter> m_spillEmitter;
	AssemblyCallbacks m_assemblyCallbacks;
	StackData m_stackData;
	Stack<AssemblyCallbacks> m_stack;
	std::map<InstId, AbstractAssembly::LabelID> m_returnLabels;
};

}
