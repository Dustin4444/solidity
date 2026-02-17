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

#include <libyul/backends/evm/AbstractAssembly.h>
#include <libyul/backends/evm/ssa/Stack.h>

#include <libevmasm/Instruction.h>

#include <cstddef>

namespace solidity::yul
{
struct BuiltinContext;
}
namespace solidity::yul::ssa
{

struct AssemblyCallbacks
{
	void swap(std::size_t const _depth)
	{
		assembly->appendInstruction(evmasm::swapInstruction(static_cast<unsigned>(_depth)));
	}

	void pop()
	{
		assembly->appendInstruction(evmasm::Instruction::POP);
	}

	void push(StackSlot const& _slot)
	{
		switch (_slot.kind())
		{
		case StackSlot::Kind::ValueID:
		{
			auto const id = _slot.valueID();
			yulAssert(id.isLiteral(), fmt::format("Tried bringing up v{}", id.value()));
			assembly->appendConstant(cfg->literalInfo(id).value);
			return;
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
			auto const& call = callSites->functionCall(_slot.functionCallReturnLabel());
			assembly->appendLabelReference(returnLabels->at(&call));
			return;
		}
		case StackSlot::Kind::FunctionReturnLabel:
		{
			//auto const* maybeLabel = util::valueOrNullptr(*returnLabels, _label.functionCall);
			//yulAssert(maybeLabel);
			//assembly->appendLabelReference(*maybeLabel);
			// todo
			return;
		}
		}
	}

	void dup(size_t const _depth)
	{
		assembly->appendInstruction(evmasm::dupInstruction(static_cast<unsigned>(_depth)));
	}

	SSACFG const* cfg{};
	AbstractAssembly* assembly{};
	CallSites const* callSites{};
	std::map<FunctionCall const*, AbstractAssembly::LabelID> const* returnLabels{};
};
static_assert(StackManipulationCallbackConcept<AssemblyCallbacks>);

class CodeTransform
{
public:
	/// Use named labels for functions 1) Yes and check that the names are unique
	/// 2) For none of the functions 3) for the first function of each name.
	enum class UseNamedLabels { YesAndForceUnique, Never, ForFirstFunctionOfEachName };

	static std::vector<StackTooDeepError> run(
		AbstractAssembly& _assembly,
		ControlFlowLiveness const& _liveness,
		BuiltinContext& _builtinContext,
		UseNamedLabels _useNamedLabelsForFunctions
	);

private:
	using FunctionLabels = std::map<Scope::Function const*, AbstractAssembly::LabelID>;

	static FunctionLabels registerFunctionLabels(
		AbstractAssembly& _assembly,
		ControlFlow const& _controlFlow,
		UseNamedLabels _useNamedLabelsForFunctions
	);

	CodeTransform(AbstractAssembly& _assembly, BuiltinContext& _builtinContext, SSACFG const& _cfg):
		m_assembly(_assembly), m_builtinContext(_builtinContext), m_cfg(_cfg) {}

	AbstractAssembly& m_assembly;
	BuiltinContext& m_builtinContext;
	SSACFG const& m_cfg;
};

}
