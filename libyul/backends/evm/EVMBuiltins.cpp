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

#include <libyul/backends/evm/EVMBuiltins.h>

#include <libyul/Object.h>
#include <libyul/Utilities.h>

#include <libevmasm/AssemblyItem.h>

#include <libsolutil/StringUtils.h>

#include <range/v3/algorithm/all_of.hpp>

using namespace solidity;
using namespace solidity::yul;

namespace
{

BuiltinFunctionForEVM createFunction(
	std::string const& _name,
	size_t _params,
	size_t _returns,
	SideEffects _sideEffects,
	ControlFlowSideEffects _controlFlowSideEffects,
	std::vector<std::optional<LiteralKind>> _literalArguments,
	std::function<void(std::vector<LiteralValue> const&, AbstractAssembly&, BuiltinContext&)> _generateCode
)
{
	yulAssert(_literalArguments.size() == _params || _literalArguments.empty(), "");

	BuiltinFunctionForEVM f;
	f.name = _name;
	f.numParameters = _params;
	f.numReturns = _returns;
	f.sideEffects = _sideEffects;
	f.controlFlowSideEffects = _controlFlowSideEffects;
	f.literalArguments = std::move(_literalArguments);
	f.isMSize = false;
	f.instruction = {};
	f.generateCode = std::move(_generateCode);
	return f;
}

BuiltinFunctionForEVM instructionBuiltin(evmasm::Instruction const& _instruction, langutil::EVMVersion const& _evmVersion)
{
	evmasm::InstructionInfo const info = evmasm::instructionInfo(_instruction, _evmVersion);
	BuiltinFunctionForEVM f;
	f.name = util::toLower(info.name);
	f.numParameters = static_cast<size_t>(info.args);
	f.numReturns = static_cast<size_t>(info.ret);
	f.sideEffects = EVMBuiltins::sideEffectsOfInstruction(_instruction);
	f.controlFlowSideEffects = ControlFlowSideEffects::fromInstruction(_instruction);
	f.isMSize = _instruction == evmasm::Instruction::MSIZE;
	f.literalArguments.clear();
	f.instruction = _instruction;
	f.generateCode = [_instruction](
		std::vector<LiteralValue> const&,
		AbstractAssembly& _assembly,
		BuiltinContext&
	)
	{
		_assembly.appendInstruction(_instruction);
	};
	return f;
}

BuiltinFunctionForEVM linkersymbolBuiltin()
{
	return createFunction(
		"linkersymbol",
		1,
		1,
		SideEffects{},
		ControlFlowSideEffects{},
		{LiteralKind::String},
		[](std::vector<LiteralValue> const& _literalArgs, AbstractAssembly& _assembly, BuiltinContext&) {
			yulAssert(_literalArgs.size() == 1, "");
			_assembly.appendLinkerSymbol(formatLiteral(_literalArgs.front()));
		}
	);
}

BuiltinFunctionForEVM memoryguardBuiltin()
{
	return createFunction(
		"memoryguard",
		1,
		1,
		SideEffects{},
		ControlFlowSideEffects{},
		{LiteralKind::Number},
		[](std::vector<LiteralValue> const& _literalArgs, AbstractAssembly& _assembly, BuiltinContext&) {
			yulAssert(_literalArgs.size() == 1, "");
			_assembly.appendConstant(_literalArgs.front().value());
		}
	);
}

BuiltinFunctionForEVM datasizeBuiltin()
{
	return createFunction(
		"datasize",
		1,
		1,
		SideEffects{},
		ControlFlowSideEffects{},
		{LiteralKind::String},
		[](std::vector<LiteralValue> const& _literalArgs, AbstractAssembly& _assembly, BuiltinContext& _context) {
			yulAssert(_context.currentObject, "No object available.");
			yulAssert(_literalArgs.size() == 1, "");
			YulName const dataName (formatLiteral(_literalArgs.front()));
			if (_context.currentObject->name == dataName.str())
				_assembly.appendAssemblySize();
			else
			{
				std::vector<AbstractAssembly::SubID> subIdPath =
					_context.subIDs.count(dataName.str()) == 0 ?
						_context.currentObject->pathToSubObject(dataName.str()) :
						std::vector{_context.subIDs.at(dataName.str())};
				yulAssert(!subIdPath.empty(), "Could not find assembly object <" + dataName.str() + ">.");
				_assembly.appendDataSize(subIdPath);
			}
		}
	);
}

BuiltinFunctionForEVM dataoffsetBuiltin()
{
	return createFunction("dataoffset", 1, 1, SideEffects{}, ControlFlowSideEffects{}, {LiteralKind::String}, [](
		std::vector<LiteralValue> const& _literalArgs,
		AbstractAssembly& _assembly,
		BuiltinContext& _context
	) {
		yulAssert(_context.currentObject, "No object available.");
		yulAssert(_literalArgs.size() == 1, "");
		YulName const dataName (formatLiteral(_literalArgs.front()));
		if (_context.currentObject->name == dataName.str())
			_assembly.appendConstant(0);
		else
		{
			std::vector<AbstractAssembly::SubID> subIdPath =
				_context.subIDs.count(dataName.str()) == 0 ?
					_context.currentObject->pathToSubObject(dataName.str()) :
					std::vector{_context.subIDs.at(dataName.str())};
			yulAssert(!subIdPath.empty(), "Could not find assembly object <" + dataName.str() + ">.");
			_assembly.appendDataOffset(subIdPath);
		}
	});
}

BuiltinFunctionForEVM datacopyBuiltin()
{
	return createFunction(
		"datacopy",
		3,
		0,
		EVMBuiltins::sideEffectsOfInstruction(evmasm::Instruction::CODECOPY),
		ControlFlowSideEffects::fromInstruction(evmasm::Instruction::CODECOPY),
		{},
		[](
			std::vector<LiteralValue> const&,
			AbstractAssembly& _assembly,
			BuiltinContext&
		) {
			_assembly.appendInstruction(evmasm::Instruction::CODECOPY);
		}
	);
}

BuiltinFunctionForEVM setimmutableBuiltin()
{
	return createFunction(
		"setimmutable",
		3,
		0,
		SideEffects{
			false,               // movable
			false,               // movableApartFromEffects
			false,               // canBeRemoved
			false,               // canBeRemovedIfNotMSize
			true,                // cannotLoop
			SideEffects::None,   // otherState
			SideEffects::None,   // storage
			SideEffects::Write,  // memory
			SideEffects::None    // transientStorage
		},
		ControlFlowSideEffects{},
		{std::nullopt, LiteralKind::String, std::nullopt},
		[](
			std::vector<LiteralValue> const& _literalArgs,
			AbstractAssembly& _assembly,
			BuiltinContext&
		) {
			yulAssert(_literalArgs.size() == 1, "");
			auto const identifier = formatLiteral(_literalArgs.front());
			_assembly.appendImmutableAssignment(identifier);
		}
	);
}

BuiltinFunctionForEVM loadimmutableBuiltin()
{
	return createFunction(
		"loadimmutable",
		1,
		1,
		SideEffects{},
		ControlFlowSideEffects{},
		{LiteralKind::String},
		[](
			std::vector<LiteralValue> const& _literalArgs,
			AbstractAssembly& _assembly,
			BuiltinContext&
		) {
			yulAssert(_literalArgs.size() == 1, "");
			_assembly.appendImmutable(formatLiteral(_literalArgs.front()));
		}
	);
}
}

void yul::generateBuiltinCode(
	BuiltinFunctionForEVM const& _builtin,
	std::vector<LiteralValue> const& _literalArguments,
	AbstractAssembly& _assembly,
	BuiltinContext& _context
)
{
	if (_builtin.instruction)
		_assembly.appendInstruction(*_builtin.instruction);
	else
		_builtin.generateCode(_literalArguments, _assembly, _context);
}

EVMBuiltins::EVMBuiltins()
{
	for (auto const& [name, opcode]: evmasm::c_instructions)
	{
		if (
			evmasm::SemanticInformation::isSwapInstruction(opcode) ||
			evmasm::SemanticInformation::isDupInstruction(opcode)
		)
			continue;

		// difficulty was replaced by prevrandao after london
		if (opcode == evmasm::Instruction::PREVRANDAO && name == "DIFFICULTY")
			m_scopesAndFunctions.emplace_back(instruction, instructionBuiltin(opcode, langutil::EVMVersion::london()));
		else
			m_scopesAndFunctions.emplace_back(instruction, instructionBuiltin(opcode, langutil::EVMVersion::current()));
	}

	m_scopesAndFunctions.emplace_back(objectAccess, linkersymbolBuiltin());
	m_scopesAndFunctions.emplace_back(objectAccess, memoryguardBuiltin());

	m_scopesAndFunctions.emplace_back(objectAccess, datasizeBuiltin());
	m_scopesAndFunctions.emplace_back(objectAccess, dataoffsetBuiltin());
	m_scopesAndFunctions.emplace_back(objectAccess, datacopyBuiltin());
	m_scopesAndFunctions.emplace_back(objectAccess, setimmutableBuiltin());
	m_scopesAndFunctions.emplace_back(objectAccess, loadimmutableBuiltin());

	static size_t constexpr verbatimPrefixLength = std::char_traits<char>::length("verbatim_");
	for (auto const& [scope, builtin]: m_scopesAndFunctions)
	{
		yulAssert(
			builtin.name.substr(0, verbatimPrefixLength) != "verbatim_",
			"Builtin functions besides verbatim should not start with the verbatim_ prefix."
		);
	}
}

BuiltinFunctionForEVM EVMBuiltins::createVerbatimFunction(size_t const _arguments, size_t const _returnVariables)
{
	BuiltinFunctionForEVM builtinFunction = createFunction(
		"verbatim_" + std::to_string(_arguments) + "i_" + std::to_string(_returnVariables) + "o",
		1 + _arguments,
		_returnVariables,
		SideEffects::worst(),
		ControlFlowSideEffects::worst(), // Worst control flow side effects because verbatim can do anything.
		std::vector<std::optional<LiteralKind>>{LiteralKind::String} + std::vector<std::optional<LiteralKind>>(_arguments),
		[=](
			std::vector<LiteralValue> const& _literalArgs,
			AbstractAssembly& _assembly,
			BuiltinContext&
		) {
			yulAssert(_literalArgs.size() == 1, "");

			_assembly.appendVerbatim(
				util::asBytes(formatLiteral(_literalArgs.front())),
				_arguments,
				_returnVariables
			);
		}
	);
	builtinFunction.isMSize = true;
	return builtinFunction;
}

SideEffects EVMBuiltins::sideEffectsOfInstruction(evmasm::Instruction _instruction)
{
	auto translate = [](evmasm::SemanticInformation::Effect _e) -> SideEffects::Effect
	{
		return static_cast<SideEffects::Effect>(_e);
	};

	return SideEffects{
		evmasm::SemanticInformation::movable(_instruction),
		evmasm::SemanticInformation::movableApartFromEffects(_instruction),
		evmasm::SemanticInformation::canBeRemoved(_instruction),
		evmasm::SemanticInformation::canBeRemovedIfNoMSize(_instruction),
		true, // cannotLoop
		translate(evmasm::SemanticInformation::otherState(_instruction)),
		translate(evmasm::SemanticInformation::storage(_instruction)),
		translate(evmasm::SemanticInformation::memory(_instruction)),
		translate(evmasm::SemanticInformation::transientStorage(_instruction)),
	};
}
