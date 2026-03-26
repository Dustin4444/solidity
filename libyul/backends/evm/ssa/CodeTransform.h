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
#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackLayout.h>

#include <libyul/backends/evm/AbstractAssembly.h>

#include <libevmasm/Instruction.h>


namespace solidity::yul
{
struct BuiltinContext;
}
namespace solidity::yul::ssa
{

struct AssemblyCallbacks
{
	void swap(StackDepth _depth);
	void pop();
	void push(StackSlot const& _slot);
	void dup(StackDepth _depth);

	SSACFG const* cfg{};
	AbstractAssembly* assembly{};
	CallSites const* callSites{};
	std::map<FunctionCall const*, AbstractAssembly::LabelID> const* returnLabels{};
};
static_assert(StackManipulationCallbackConcept<AssemblyCallbacks>);

class CodeTransform
{
public:
	static void run(
		AbstractAssembly& _assembly,
		ControlFlowLiveness const& _liveness,
		BuiltinContext& _builtinContext
	);

private:
	using FunctionLabels = std::map<Scope::Function const*, AbstractAssembly::LabelID>;

	static FunctionLabels registerFunctionLabels(
		AbstractAssembly& _assembly,
		ControlFlow const& _controlFlow
	);

	CodeTransform(
		AbstractAssembly& _assembly,
		BuiltinContext& _builtinContext,
		FunctionLabels const& _functionLabels,
		CallSites const& _callSites,
		SSACFG const& _cfg,
		SSACFGStackLayout const& _stackLayout,
		Scope::Function const* _function,
		ControlFlow::FunctionGraphID _graphID);

	void operator()(SSACFG::BlockId _blockId);
	void operator()(SSACFG::OperationId _opId, StackData const& _operationInputLayout);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::MainExit const& _mainExit);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::ConditionalJump const& _conditionalJump);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::Jump const& _jump);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::FunctionReturn const& _functionReturn);
	void operator()(SSACFG::BlockId const& _currentBlock, SSACFG::BasicBlock::Terminated const& _terminated);

	void prepareBlockExitStack(StackData const& _target, PhiInverse const& _phiInverse);

	AbstractAssembly& m_assembly;
	BuiltinContext& m_builtinContext;
	FunctionLabels const& m_functionLabels;
	CallSites const& m_callSites;
	SSACFG const& m_cfg;
	SSACFGStackLayout const& m_stackLayout;
	ControlFlow::FunctionGraphID const m_graphID;

	std::vector<std::uint8_t> m_blockIsTransformed;
	std::vector<AbstractAssembly::LabelID> m_blockLabels;
	AssemblyCallbacks m_assemblyCallbacks;
	StackData m_stackData;
	Stack<AssemblyCallbacks> m_stack;
	std::map<FunctionCall const*, AbstractAssembly::LabelID> m_returnLabels;
};

}
