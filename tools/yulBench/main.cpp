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

#include "libyul/backends/evm/SSACFGEVMCodeTransform.h"
#include "libyul/backends/evm/ssa/StackLayoutGenerator.h"
#include "libyul/backends/evm/ssa/StackUtils.h"

#include <libyul/YulStack.h>
#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/backends/evm/ssa/SSACFG.h>

#include <liblangutil/ErrorReporter.h>

#include <benchmark/benchmark.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <libyul/backends/evm/ssa/ControlFlow.h>
#include <libyul/backends/evm/ssa/SSACFGBuilder.h>

std::shared_ptr<solidity::yul::Object> parseFromString(std::string const& _source)
{
	solidity::yul::YulStack stack(
		solidity::langutil::EVMVersion::current(),
		std::nullopt,
		solidity::yul::Language::StrictAssembly,
		solidity::frontend::OptimiserSettings::standard(),
		solidity::langutil::DebugInfoSelection::None()
	);
	stack.parseAndAnalyze("", _source);
	return stack.parserResult();
}

std::shared_ptr<solidity::yul::Object> parse(std::string_view _filePath)
{
	auto const fileContents = [&]
	{
		std::filesystem::path const file(_filePath);
		if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file))
			throw std::runtime_error("File does not exist.");
		std::string result;
		if (std::ifstream is{file, std::ios::binary | std::ios::ate})
		{
			auto size = is.tellg();
			result = std::string(size, '\0');
			is.seekg(0);
			if (!is.read(&result[0], size))
				throw std::runtime_error("Failed to read file.");
		}
		else
			throw std::runtime_error("Failed to open file.");
		return result;
	}();
	solidity::yul::YulStack stack (
		solidity::langutil::EVMVersion::current(),
		std::nullopt,
		solidity::yul::Language::StrictAssembly,
		solidity::frontend::OptimiserSettings::standard(),
		solidity::langutil::DebugInfoSelection::None()
	);
	stack.parseAndAnalyze("", fileContents);
	return stack.parserResult();
}

std::unique_ptr<solidity::yul::ssa::ControlFlow> buildSSACFG(solidity::yul::Object const& _object)
{
	std::unique_ptr<solidity::yul::ssa::ControlFlow> controlFlow = solidity::yul::ssa::SSACFGBuilder::build(
		*_object.analysisInfo,
		*_object.dialect(),
		_object.code()->root(),
		false,
		false
	);
	return controlFlow;
}

solidity::yul::ssa::ControlFlowLiveness computeLiveness(solidity::yul::ssa::ControlFlow const& _controlFlow)
{
	return solidity::yul::ssa::ControlFlowLiveness(_controlFlow);
}

struct BenchInput
{
	std::string name;
	std::shared_ptr<solidity::yul::Object> object;
};

std::vector<BenchInput> loadInputsFromJson(std::string const& _jsonPath)
{
	std::ifstream file(_jsonPath);
	if (!file)
		throw std::runtime_error("Failed to open JSON file: " + _jsonPath);

	nlohmann::json json;
	file >> json;

	std::vector<BenchInput> result;
	for (auto const& [fileName, fileContracts]: json["contracts"].items())
		for (auto const& [contractName, contractData]: fileContracts.items())
		{
			if (!contractData.contains("ir"))
				continue;
			auto ir = contractData["ir"].get<std::string>();
			if (ir.empty())
				continue;
			try
			{
				auto obj = parseFromString(ir);
				if (obj)
					result.push_back({contractName, std::move(obj)});
			}
			catch (std::exception const& e)
			{
				std::cerr << "Skipping " << contractName << ": " << e.what() << "\n";
			}
		}
	return result;
}

using Stats = solidity::yul::ssa::SSACFGBuilder::Stats;

void printBuildStats(std::vector<BenchInput> const& _inputs)
{
	// Column widths
	constexpr int W_NAME  = 36;
	constexpr int W_NUM   = 10;
	constexpr int W_RATIO =  8;

	auto hr = [&]{ std::cerr << std::string(W_NAME + 7*W_NUM + 3*W_RATIO + 3, '-') << "\n"; };

	std::cerr << "\n";
	hr();
	std::cerr
		<< std::left  << std::setw(W_NAME)  << "contract"
		<< std::right << std::setw(W_NUM)   << "ups_emit"
		<< std::setw(W_NUM)  << "rm_calls"
		<< std::setw(W_NUM)  << "rm_ok"
		<< std::setw(W_NUM)  << "chk_iters"
		<< std::setw(W_RATIO)<< "chk/call"
		<< std::setw(W_NUM)  << "blk_scan"
		<< std::setw(W_RATIO)<< "blk/rm"
		<< std::setw(W_NUM)  << "ups_scan"
		<< std::setw(W_RATIO)<< "ups/blk"
		<< std::setw(W_NUM)  << "opinputs"
		<< std::setw(W_NUM)  << "unreach"
		<< std::setw(W_NUM+2)<< "tryRm_us"
		<< std::setw(W_NUM+2)<< "clean_us"
		<< "\n";
	hr();

	Stats totals{};
	for (auto const& input: _inputs)
	{
		buildSSACFG(*input.object);
		Stats const& s = solidity::yul::ssa::SSACFGBuilder::s_stats;

		auto ratio = [](size_t num, size_t den) -> double {
			return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
		};

		std::cerr
			<< std::left  << std::setw(W_NAME)  << input.name.substr(0, W_NAME - 1)
			<< std::right << std::setw(W_NUM)   << s.upsilonsEmitted
			<< std::setw(W_NUM)  << s.tryRemoveCalls
			<< std::setw(W_NUM)  << s.tryRemoveSucceeded
			<< std::setw(W_NUM)  << s.trivialCheckIterations
			<< std::fixed << std::setprecision(1)
			<< std::setw(W_RATIO)<< ratio(s.trivialCheckIterations, s.tryRemoveCalls)
			<< std::defaultfloat
			<< std::setw(W_NUM)  << s.replacementBlocksScanned
			<< std::fixed << std::setprecision(1)
			<< std::setw(W_RATIO)<< ratio(s.replacementBlocksScanned, s.tryRemoveSucceeded)
			<< std::defaultfloat
			<< std::setw(W_NUM)  << s.replacementUpsilonsScanned
			<< std::fixed << std::setprecision(1)
			<< std::setw(W_RATIO)<< ratio(s.replacementUpsilonsScanned, s.replacementBlocksScanned)
			<< std::defaultfloat
			<< std::setw(W_NUM)  << s.replacementOpInputsScanned
			<< std::setw(W_NUM)  << s.cleanUnreachableRemovals
			<< std::setw(W_NUM)  << (s.tryRemoveNs / 1000) << "us"
			<< std::setw(W_NUM)  << (s.cleanUnreachableNs / 1000) << "us"
			<< "\n";

		totals.upsilonsEmitted           += s.upsilonsEmitted;
		totals.tryRemoveCalls            += s.tryRemoveCalls;
		totals.tryRemoveSucceeded        += s.tryRemoveSucceeded;
		totals.trivialCheckIterations    += s.trivialCheckIterations;
		totals.replacementBlocksScanned  += s.replacementBlocksScanned;
		totals.replacementUpsilonsScanned  += s.replacementUpsilonsScanned;
		totals.replacementOpInputsScanned  += s.replacementOpInputsScanned;
		totals.cleanUnreachableRemovals    += s.cleanUnreachableRemovals;
		totals.tryRemoveNs                 += s.tryRemoveNs;
		totals.cleanUnreachableNs          += s.cleanUnreachableNs;
	}

	hr();
	auto ratio = [](size_t num, size_t den) -> double {
		return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
	};
	std::cerr
		<< std::left  << std::setw(W_NAME)  << "TOTAL"
		<< std::right << std::setw(W_NUM)   << totals.upsilonsEmitted
		<< std::setw(W_NUM)  << totals.tryRemoveCalls
		<< std::setw(W_NUM)  << totals.tryRemoveSucceeded
		<< std::setw(W_NUM)  << totals.trivialCheckIterations
		<< std::fixed << std::setprecision(1)
		<< std::setw(W_RATIO)<< ratio(totals.trivialCheckIterations, totals.tryRemoveCalls)
		<< std::defaultfloat
		<< std::setw(W_NUM)  << totals.replacementBlocksScanned
		<< std::fixed << std::setprecision(1)
		<< std::setw(W_RATIO)<< ratio(totals.replacementBlocksScanned, totals.tryRemoveSucceeded)
		<< std::defaultfloat
		<< std::setw(W_NUM)  << totals.replacementUpsilonsScanned
		<< std::fixed << std::setprecision(1)
		<< std::setw(W_RATIO)<< ratio(totals.replacementUpsilonsScanned, totals.replacementBlocksScanned)
		<< std::defaultfloat
		<< std::setw(W_NUM)  << totals.replacementOpInputsScanned
		<< std::setw(W_NUM)  << totals.cleanUnreachableRemovals
		<< std::setw(W_NUM)  << (totals.tryRemoveNs / 1000) << "us"
		<< std::setw(W_NUM)  << (totals.cleanUnreachableNs / 1000) << "us"
		<< "\n";
	hr();
	std::cerr << "\n";
}

int main(int argc, char** argv)
{
	// First non-flag argument (if any) is taken as the JSON path.
	std::string jsonPath = "out.json";
	bool jsonPathSet = false;
	std::vector<char*> benchArgv = {argv[0]};
	for (int i = 1; i < argc; ++i)
	{
		std::string_view arg(argv[i]);
		if (!arg.starts_with("--") && !jsonPathSet)
		{
			jsonPath = std::string(arg);
			jsonPathSet = true;
		}
		else
			benchArgv.push_back(argv[i]);
	}
	int benchArgc = static_cast<int>(benchArgv.size());

	std::cerr << "Loading benchmarks from: " << jsonPath << "\n";
	auto inputs = loadInputsFromJson(jsonPath);
	std::cerr << "Loaded " << inputs.size() << " contracts.\n";

	// If --benchmark_filter is set, restrict the stats table to matching contracts.
	// Fall back to all contracts if the filter matches none (e.g. a skip-all pattern).
	std::string filterPattern;
	for (auto const* arg: benchArgv)
		if (std::string_view sv(arg); sv.starts_with("--benchmark_filter="))
			filterPattern = std::string(sv.substr(std::string_view("--benchmark_filter=").size()));

	std::vector<BenchInput> statsInputs;
	for (auto const& input: inputs)
		if (filterPattern.empty() ||
			("BM_BuildSSACFG/" + input.name).find(filterPattern) != std::string::npos)
			statsInputs.push_back(input);
	if (statsInputs.empty())
		statsInputs = inputs;
	printBuildStats(statsInputs);

	for (auto const& input: inputs)
		benchmark::RegisterBenchmark(
			"BM_BuildSSACFG/" + input.name,
			[obj = input.object](benchmark::State& state)
			{
				for (auto _: state)
				{
					auto cfg = buildSSACFG(*obj);
					benchmark::DoNotOptimize(cfg);
				}
			}
		);

	::benchmark::Initialize(&benchArgc, benchArgv.data());
	if (::benchmark::ReportUnrecognizedArguments(benchArgc, benchArgv.data()))
		return 1;
	::benchmark::RunSpecifiedBenchmarks();
	::benchmark::Shutdown();
	return 0;
}
