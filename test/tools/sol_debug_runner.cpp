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
 * Standalone debug tool that reproduces the sol_proto2_ossfuzz fuzzer's
 * compile-deploy-execute flow on a .sol file. Runs all 4 configurations
 * ({noOpt, opt} x {viaIR=true, viaIR=false}) and dumps bytecodes, logs,
 * storage, and output for debugging differential testing failures.
 */

#include <test/tools/ossfuzz/SolidityEvmoneInterface.h>
#include <test/EVMHost.h>

#include <libevmasm/Exceptions.h>

#include <boost/program_options.hpp>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <map>

using namespace solidity::test::fuzzer;
using namespace solidity::test;
using namespace solidity::frontend;
using namespace solidity::langutil;
using namespace solidity::util;
using namespace solidity;

namespace po = boost::program_options;

static constexpr int64_t s_gasLimit = 1000000;

/// Result of a single compile-deploy-execute run.
struct RunResult
{
	bool compilationFailed = false;
	bytes bytecode;
	evmc_status_code statusCode = EVMC_INTERNAL_ERROR;
	bytes output;
	std::vector<evmc::MockedHost::log_record> logs;
	std::map<evmc::address, StorageMap> storage;
};

static std::string statusCodeToString(evmc_status_code _code)
{
	switch (_code)
	{
	case EVMC_SUCCESS: return "SUCCESS";
	case EVMC_FAILURE: return "FAILURE";
	case EVMC_REVERT: return "REVERT";
	case EVMC_OUT_OF_GAS: return "OUT_OF_GAS";
	case EVMC_INVALID_INSTRUCTION: return "INVALID_INSTRUCTION";
	case EVMC_UNDEFINED_INSTRUCTION: return "UNDEFINED_INSTRUCTION";
	case EVMC_STACK_OVERFLOW: return "STACK_OVERFLOW";
	case EVMC_STACK_UNDERFLOW: return "STACK_UNDERFLOW";
	case EVMC_BAD_JUMP_DESTINATION: return "BAD_JUMP_DESTINATION";
	case EVMC_INVALID_MEMORY_ACCESS: return "INVALID_MEMORY_ACCESS";
	case EVMC_CALL_DEPTH_EXCEEDED: return "CALL_DEPTH_EXCEEDED";
	case EVMC_STATIC_MODE_VIOLATION: return "STATIC_MODE_VIOLATION";
	case EVMC_PRECOMPILE_FAILURE: return "PRECOMPILE_FAILURE";
	case EVMC_CONTRACT_VALIDATION_FAILURE: return "CONTRACT_VALIDATION_FAILURE";
	case EVMC_ARGUMENT_OUT_OF_RANGE: return "ARGUMENT_OUT_OF_RANGE";
	case EVMC_INTERNAL_ERROR: return "INTERNAL_ERROR";
	case EVMC_REJECTED: return "REJECTED";
	case EVMC_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
	default: return "UNKNOWN(" + std::to_string(static_cast<int>(_code)) + ")";
	}
}

static std::string toHexString(bytes const& _data)
{
	std::ostringstream ss;
	for (uint8_t b : _data)
		ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
	return ss.str();
}

static std::string toHexString(evmc::bytes32 const& _data)
{
	std::ostringstream ss;
	for (uint8_t b : _data.bytes)
		ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
	return ss.str();
}

static std::string toHexString(evmc::address const& _addr)
{
	std::ostringstream ss;
	for (uint8_t b : _addr.bytes)
		ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
	return ss.str();
}

static RunResult runOnce(
	evmc::VM& _vm,
	EVMVersion _version,
	StringMap const& _source,
	OptimiserSettings _optimiserSettings,
	bool _viaIR
)
{
	RunResult result;
	EVMHost hostContext(_version, _vm);
	std::string contractName = "C";
	std::string methodName = "test()";

	CompilerInput cInput(
		_version,
		_source,
		contractName,
		_optimiserSettings,
		{},
		/*debugFailure=*/true,
		_viaIR
	);

	// First, compile separately to extract bytecode for dumping.
	// Scoped so the CompilerStack is destroyed before EvmoneUtility creates its own.
	{
		SolidityCompilationFramework compiler(cInput);
		auto compOutput = compiler.compileContract();
		if (compOutput.has_value() && !compOutput->byteCode.empty())
			result.bytecode = compOutput->byteCode;
	}

	EvmoneUtility evmoneUtil(
		hostContext,
		cInput,
		contractName,
		/*libraryName=*/"",
		methodName,
		s_gasLimit
	);

	evmc::Result evmResult = evmoneUtil.compileDeployAndExecute();
	result.statusCode = evmResult.status_code;
	if (evmResult.output_data && evmResult.output_size > 0)
		result.output = bytes(evmResult.output_data, evmResult.output_data + evmResult.output_size);

	// Capture logs
	result.logs.assign(hostContext.recorded_logs.begin(), hostContext.recorded_logs.end());

	// Capture storage
	for (auto const& [addr, account] : hostContext.accounts)
		if (!account.storage.empty())
			result.storage[addr] = account.storage;

	return result;
}

static bool logsEqual(
	std::vector<evmc::MockedHost::log_record> const& _a,
	std::vector<evmc::MockedHost::log_record> const& _b
)
{
	if (_a.size() != _b.size())
		return false;
	for (size_t i = 0; i < _a.size(); i++)
		if (!(_a[i] == _b[i]))
			return false;
	return true;
}

static bool storageEqual(
	std::map<evmc::address, StorageMap> const& _a,
	std::map<evmc::address, StorageMap> const& _b
)
{
	if (_a.size() != _b.size())
		return false;
	for (auto const& [addr, storageA] : _a)
	{
		auto it = _b.find(addr);
		if (it == _b.end())
			return false;
		auto const& storageB = it->second;
		if (storageA.size() != storageB.size())
			return false;
		for (auto const& [key, valA] : storageA)
		{
			auto jt = storageB.find(key);
			if (jt == storageB.end())
				return false;
			if (valA.current != jt->second.current)
				return false;
		}
	}
	return true;
}

static void printRunResult(std::string const& _label, RunResult const& _run, std::ostream& _out)
{
	_out << "=== " << _label << " ===" << std::endl;

	if (_run.compilationFailed)
	{
		_out << "  COMPILATION FAILED" << std::endl;
		return;
	}

	_out << "  Bytecode size: " << _run.bytecode.size() << " bytes" << std::endl;
	_out << "  Bytecode: " << toHexString(_run.bytecode) << std::endl;
	_out << "  Status: " << statusCodeToString(_run.statusCode) << std::endl;
	_out << "  Output (" << _run.output.size() << " bytes): " << toHexString(_run.output) << std::endl;

	_out << "  Logs (" << _run.logs.size() << "):" << std::endl;
	for (size_t i = 0; i < _run.logs.size(); i++)
	{
		auto const& log = _run.logs[i];
		_out << "    Log[" << i << "]:" << std::endl;
		_out << "      Creator: " << toHexString(log.creator) << std::endl;
		_out << "      Data (" << log.data.size() << " bytes): ";
		std::ostringstream dataSS;
		for (uint8_t b : log.data)
			dataSS << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
		_out << dataSS.str() << std::endl;
		_out << "      Topics (" << log.topics.size() << "):" << std::endl;
		for (size_t j = 0; j < log.topics.size(); j++)
			_out << "        [" << j << "]: " << toHexString(log.topics[j]) << std::endl;
	}

	_out << "  Storage (" << _run.storage.size() << " accounts):" << std::endl;
	for (auto const& [addr, storageMap] : _run.storage)
	{
		_out << "    Account " << toHexString(addr) << " (" << storageMap.size() << " slots):" << std::endl;
		for (auto const& [key, val] : storageMap)
			_out << "      " << toHexString(key) << " => " << toHexString(val.current) << std::endl;
	}
	_out << std::endl;
}

static void compareRuns(
	std::string const& _labelA,
	RunResult const& _a,
	std::string const& _labelB,
	RunResult const& _b
)
{
	std::cout << "--- Comparing " << _labelA << " vs " << _labelB << " ---" << std::endl;

	if (_a.compilationFailed || _b.compilationFailed)
	{
		std::cout << "  SKIPPED (compilation failed: "
			<< _labelA << "=" << (_a.compilationFailed ? "yes" : "no") << ", "
			<< _labelB << "=" << (_b.compilationFailed ? "yes" : "no") << ")"
			<< std::endl;
		return;
	}

	if (_a.statusCode == EVMC_OUT_OF_GAS || _b.statusCode == EVMC_OUT_OF_GAS)
	{
		std::cout << "  SKIPPED (out-of-gas: "
			<< _labelA << "=" << statusCodeToString(_a.statusCode) << ", "
			<< _labelB << "=" << statusCodeToString(_b.statusCode) << ")"
			<< std::endl;
		return;
	}

	// Status code
	bool statusMatch = (_a.statusCode == _b.statusCode);
	std::cout << "  Status:  " << (statusMatch ? "MATCH" : "DIFFER")
		<< " (" << statusCodeToString(_a.statusCode) << " vs " << statusCodeToString(_b.statusCode) << ")"
		<< std::endl;

	if (_a.statusCode == EVMC_SUCCESS && _b.statusCode == EVMC_SUCCESS)
	{
		// Output
		bool outputMatch = (_a.output.size() == _b.output.size() &&
			std::memcmp(_a.output.data(), _b.output.data(), _a.output.size()) == 0);
		std::cout << "  Output:  " << (outputMatch ? "MATCH" : "DIFFER") << std::endl;

		// Logs
		bool logsMatch = logsEqual(_a.logs, _b.logs);
		std::cout << "  Logs:    " << (logsMatch ? "MATCH" : "DIFFER") << std::endl;

		// Storage
		bool storageMatch = storageEqual(_a.storage, _b.storage);
		std::cout << "  Storage: " << (storageMatch ? "MATCH" : "DIFFER") << std::endl;
	}
	std::cout << std::endl;
}

static void writeToFile(std::string const& _path, std::string const& _content)
{
	std::ofstream f(_path);
	if (!f.is_open())
	{
		std::cerr << "Error: Cannot write to " << _path << std::endl;
		return;
	}
	f << _content;
	std::cout << "  Written: " << _path << std::endl;
}

int main(int argc, char* argv[])
{
	po::options_description desc("sol_debug_runner - reproduce fuzzer compile/deploy/execute");
	desc.add_options()
		("help,h", "Show help")
		("input-file", po::value<std::string>(), "Solidity source file")
		("output-dir", po::value<std::string>()->default_value(""), "Directory to write output files (optional)")
		("via-ir", po::value<bool>()->default_value(true), "Initial viaIR setting (default: true)")
	;

	po::positional_options_description positional;
	positional.add("input-file", 1);

	po::variables_map vm;
	try
	{
		po::store(po::command_line_parser(argc, argv).options(desc).positional(positional).run(), vm);
		po::notify(vm);
	}
	catch (std::exception const& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	if (vm.count("help") || !vm.count("input-file"))
	{
		std::cout << "Usage: sol_debug_runner <file.sol> [--output-dir <dir>] [--via-ir true|false]" << std::endl;
		std::cout << desc << std::endl;
		return vm.count("help") ? 0 : 1;
	}

	std::string inputFile = vm["input-file"].as<std::string>();
	std::string outputDir = vm["output-dir"].as<std::string>();
	bool viaIR = vm["via-ir"].as<bool>();

	// Read source file
	std::ifstream ifs(inputFile);
	if (!ifs.is_open())
	{
		std::cerr << "Error: Cannot open " << inputFile << std::endl;
		return 1;
	}
	std::string solSource{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};

	std::cout << "Source file: " << inputFile << " (" << solSource.size() << " bytes)" << std::endl;
	std::cout << "viaIR: " << (viaIR ? "true" : "false") << std::endl;
	std::cout << "Gas limit: " << s_gasLimit << std::endl;
	std::cout << std::endl;

	// Load evmone VM (relies on LD_LIBRARY_PATH to find the shared library)
	evmc::VM& evmVM = EVMHost::getVM("libevmone.so");
	if (!evmVM)
	{
		std::cerr << "Error: Could not load evmone VM. Set LD_LIBRARY_PATH to include evmone lib directory." << std::endl;
		return 1;
	}

	EVMVersion version = EVMVersion::current();
	StringMap source({{"test.sol", solSource}});

	// Run 4 configurations
	struct Config
	{
		std::string label;
		OptimiserSettings optimiser;
		bool viaIR;
	};

	std::vector<Config> configs = {
		{"noOpt_viaIR=" + std::string(viaIR ? "true" : "false"), OptimiserSettings::minimal(), viaIR},
		{"opt_viaIR=" + std::string(viaIR ? "true" : "false"), OptimiserSettings::standard(), viaIR},
		{"noOpt_viaIR=" + std::string(!viaIR ? "true" : "false"), OptimiserSettings::minimal(), !viaIR},
		{"opt_viaIR=" + std::string(!viaIR ? "true" : "false"), OptimiserSettings::standard(), !viaIR},
	};

	std::vector<RunResult> results;
	for (auto const& config : configs)
	{
		std::cout << "Running: " << config.label << "..." << std::endl;
		try
		{
			results.push_back(runOnce(evmVM, version, source, config.optimiser, config.viaIR));
		}
		catch (evmasm::StackTooDeepException const&)
		{
			std::cout << "  StackTooDeep exception" << std::endl;
			RunResult r;
			r.compilationFailed = true;
			results.push_back(std::move(r));
		}
		catch (std::exception const& e)
		{
			std::cout << "  Exception: " << e.what() << std::endl;
			RunResult r;
			r.compilationFailed = true;
			results.push_back(std::move(r));
		}
	}

	std::cout << std::endl;

	// Print all results
	for (size_t i = 0; i < configs.size(); i++)
		printRunResult(configs[i].label, results[i], std::cout);

	// Run differential comparisons (same as fuzzer)
	std::cout << "========== DIFFERENTIAL COMPARISONS ==========" << std::endl << std::endl;

	// Same viaIR: noOpt vs opt
	compareRuns(configs[0].label, results[0], configs[1].label, results[1]);
	// Opposite viaIR: noOpt vs opt
	compareRuns(configs[2].label, results[2], configs[3].label, results[3]);
	// Cross viaIR: noOpt(viaIR) vs noOpt(!viaIR)
	compareRuns(configs[0].label, results[0], configs[2].label, results[2]);
	// Cross viaIR: opt(viaIR) vs opt(!viaIR)
	compareRuns(configs[1].label, results[1], configs[3].label, results[3]);

	// Write output files if requested
	if (!outputDir.empty())
	{
		std::cout << "Writing output files to: " << outputDir << std::endl;
		for (size_t i = 0; i < configs.size(); i++)
		{
			std::string prefix = outputDir + "/" + configs[i].label;
			if (!results[i].compilationFailed)
			{
				writeToFile(prefix + ".bytecode.hex", toHexString(results[i].bytecode));
				std::ostringstream logStream;
				printRunResult(configs[i].label, results[i], logStream);
				writeToFile(prefix + ".log", logStream.str());
			}
		}
	}

	return 0;
}
