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

#include <libevmasm/Ethdebug.h>

#include <libevmasm/EthdebugSchema.h>

using namespace solidity;
using namespace solidity::evmasm;
using namespace solidity::evmasm::ethdebug;

namespace
{

std::vector<schema::Instruction> programInstructions(Assembly const& _assembly, LinkerObject const& _linkerObject, unsigned _sourceId)
{
	solUnimplementedAssert(_assembly.eofVersion() == std::nullopt, "ethdebug does not yet support EOF.");
	solUnimplementedAssert(_assembly.codeSections().size() == 1, "ethdebug does not yet support multiple code-sections.");
	for (auto const& instruction: _assembly.codeSections()[0].items)
		solUnimplementedAssert(instruction.type() != VerbatimBytecode, "Verbatim bytecode is currently not supported by ethdebug.");

	solAssert(_linkerObject.codeSectionLocations.size() == 1);
	solAssert(_linkerObject.codeSectionLocations[0].end <= _linkerObject.bytecode.size());
	std::vector<schema::Instruction> instructions;
	for (size_t i = 0; i < _linkerObject.codeSectionLocations[0].instructionLocations.size(); ++i)
	{
		LinkerObject::InstructionLocation currentInstruction = _linkerObject.codeSectionLocations[0].instructionLocations[i];
		size_t start = currentInstruction.start;
		size_t end = currentInstruction.end;
		size_t assemblyItemIndex = currentInstruction.assemblyItemIndex;
		solAssert(end <= _linkerObject.bytecode.size());
		solAssert(start < end);
		solAssert(assemblyItemIndex < _assembly.codeSections().at(0).items.size());
		schema::Operation operation;
		operation.mnemonic = instructionInfo(static_cast<Instruction>(_linkerObject.bytecode[start]), _assembly.evmVersion()).name;
		static size_t constexpr instructionSize = 1;
		if (start + instructionSize < end)
		{
			bytes const argumentData(
				_linkerObject.bytecode.begin() + static_cast<std::ptrdiff_t>(start) + instructionSize,
				_linkerObject.bytecode.begin() + static_cast<std::ptrdiff_t>(end)
			);
			solAssert(!argumentData.empty());
			operation.arguments = {{schema::HexValue{argumentData}}};
		}
		langutil::SourceLocation const& location = _assembly.codeSections().at(0).items.at(assemblyItemIndex).location();
		instructions.emplace_back(schema::Instruction{
			.offset = schema::NonNegativeValue{start},
			.operation = operation,
			.context = {
				.code = schema::SourceRange{
					.source = schema::Source{schema::ID{_sourceId}},
					.range = schema::Range{
						.length = schema::NonNegativeValue{location.end - location.start},
						.offset = schema::NonNegativeValue{location.start}
					}
				},
				.variables = std::nullopt,
				.remark = std::nullopt
			}
		});
	}

	return instructions;
}

} // anonymous namespace

Json ethdebug::program(std::string_view const _name, unsigned _sourceId, Assembly const& _assembly, LinkerObject const& _linkerObject)
{
	return schema::Program{
		.id = std::nullopt,
		.contract = {
			.name = std::string{_name},
			.definition = {
				.source = {
					.id = {_sourceId}
				},
				.range = std::nullopt
			}
		},
		.environment = _assembly.isCreation() ? schema::Environment::CREATE : schema::Environment::CALL,
		.context = std::nullopt,
		.instructions = programInstructions(_assembly, _linkerObject, _sourceId)
	};
}

Json ethdebug::resources(std::vector<std::string> const& _sources, std::string const& _version)
{
	Json sources = Json::array();
	for (size_t id = 0; id < _sources.size(); ++id)
	{
		Json source = Json::object();
		source["id"] = id;
		source["path"] = _sources[id];
		sources.push_back(source);
	}
	Json result = Json::object();
	result["compilation"] = Json::object();
	result["compilation"]["compiler"] = Json::object();
	result["compilation"]["compiler"]["name"] = "solc";
	result["compilation"]["compiler"]["version"] = _version;
	result["compilation"]["sources"] = sources;
	return result;
}
