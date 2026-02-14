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

/// @file main.cpp
/// Bulk SSA CFG validation tool. Compiles .sol files to Yul IR (unoptimized
/// and optimized), then validates the SSA CFG for every object recursively.

#include <libsolidity/interface/CompilerStack.h>
#include <libsolidity/interface/OptimiserSettings.h>

#include <libyul/YulStack.h>
#include <libyul/Object.h>
#include <libyul/backends/evm/ssa/SSACFGBuilder.h>
#include <libyul/backends/evm/ssa/SSACFGValidator.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/EVMVersion.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::langutil;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

static std::string readFile(std::string_view _path)
{
	std::filesystem::path const file(_path);
	if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file))
		throw std::runtime_error("File does not exist: " + std::string(_path));
	std::ifstream is(file, std::ios::binary | std::ios::ate);
	if (!is)
		throw std::runtime_error("Failed to open file: " + std::string(_path));
	auto size = is.tellg();
	std::string result(static_cast<std::size_t>(size), '\0');
	is.seekg(0);
	if (!is.read(result.data(), size))
		throw std::runtime_error("Failed to read file: " + std::string(_path));
	return result;
}

static void validateObject(Object const& _obj, std::string const& _label, bool _dumpOnFail = false)
{
	Dialect const* dialect = _obj.dialect();
	if (!dialect || !_obj.hasCode())
		return;

	std::string objLabel = _label + "/" + _obj.name;

	// Validate with keepLiteralAssignments=false
	try
	{
		auto controlFlow = SSACFGBuilder::build(
			*_obj.analysisInfo, *dialect, _obj.code()->root(), false
		);
		if (_dumpOnFail)
		{
			// Pre-dump the graph before validation
			std::cerr << controlFlow->toDot() << std::endl;
		}
		SSACFGValidator::validate(
			*controlFlow, *_obj.analysisInfo, *dialect, _obj.code()->root(), false
		);
	}
	catch (std::exception const& e)
	{
		throw std::runtime_error(objLabel + " (keepLit=false): " + e.what());
	}

	// Validate with keepLiteralAssignments=true
	try
	{
		auto controlFlow = SSACFGBuilder::build(
			*_obj.analysisInfo, *dialect, _obj.code()->root(), true
		);
		SSACFGValidator::validate(
			*controlFlow, *_obj.analysisInfo, *dialect, _obj.code()->root(), true
		);
	}
	catch (std::exception const& e)
	{
		throw std::runtime_error(objLabel + " (keepLit=true): " + e.what());
	}

	// Recurse into sub-objects
	for (auto const& subNode: _obj.subObjects)
		if (auto const* subObj = dynamic_cast<Object const*>(subNode.get()))
			validateObject(*subObj, objLabel, _dumpOnFail);
}

static void validateYulIR(std::string const& _ir, std::string const& _label)
{
	YulStack stack(
		EVMVersion::current(),
		std::nullopt,
		Language::StrictAssembly,
		OptimiserSettings::none(),
		DebugInfoSelection::None()
	);
	if (!stack.parseAndAnalyze("", _ir))
		throw std::runtime_error("Failed to parse Yul IR for " + _label);

	auto obj = stack.parserResult();
	if (!obj)
		throw std::runtime_error("No parser result for " + _label);

	validateObject(*obj, _label, _label.find("optimized") != std::string::npos);
}

static bool processFile(std::string const& _path)
{
	std::string source = readFile(_path);

	CompilerStack compiler;
	compiler.setSources({{"source", source}});
	compiler.setViaIR(true);
	compiler.setEVMVersion(EVMVersion::current());
	compiler.setOptimiserSettings(OptimiserSettings::standard());
	// Request only IR generation (no bytecode) for efficiency
	compiler.selectContracts({{"", {{"", CompilerStack::PipelineConfig{true, true, false}}}}});

	if (!compiler.compile())
		return true; // Skip files that fail to compile

	for (std::string const& contractName: compiler.contractNames())
	{
		auto const& ir = compiler.yulIR(contractName);
		if (ir.has_value() && !ir->empty())
			validateYulIR(*ir, _path + ":" + contractName + " (unoptimized)");

		auto const& irOpt = compiler.yulIROptimized(contractName);
		if (irOpt.has_value() && !irOpt->empty())
			validateYulIR(*irOpt, _path + ":" + contractName + " (optimized)");
	}

	return true;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cerr << "Usage: yulSSAVal <file1.sol> [file2.sol ...]\n";
		return 2;
	}

	for (int i = 1; i < argc; ++i)
	{
		std::string const path = argv[i];
		try
		{
			processFile(path);
			std::cout << "PASS " << path << std::endl;
		}
		catch (std::exception const& e)
		{
			std::cout << "FAIL " << path << ": " << e.what() << std::endl;
		}
		catch (...)
		{
			std::cout << "FAIL " << path << ": unknown exception" << std::endl;
		}
	}

	return 0;
}
