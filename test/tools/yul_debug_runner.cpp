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
 * Standalone debug tool that reproduces the yul_proto_ossfuzz_evmone fuzzer's
 * compile-deploy-execute flow on a .yul file. Runs three configurations
 * (unoptimized, optimized legacy, optimized SSACFG) and dumps bytecodes, logs,
 * storage, and output for debugging differential testing failures.
 */

#include <test/tools/ossfuzz/YulEvmoneInterface.h>
#include <test/EVMHost.h>

#include <libyul/Exceptions.h>

#include <libsolidity/interface/OptimiserSettings.h>

#include <liblangutil/EVMVersion.h>

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
using namespace solidity;

namespace po = boost::program_options;

// ANSI color codes
static constexpr char const* GREEN = "\033[32m";
static constexpr char const* RED = "\033[31m";
static constexpr char const* YELLOW = "\033[33m";
static constexpr char const* RESET = "\033[0m";

static constexpr int64_t s_gasLimit = 400000;

/// Result of a single compile-deploy-execute run.
struct RunResult
{
	bool compilationFailed = false;
	bool internalError = false;
	std::string internalErrorMsg;
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

static RunResult runYulOnce(
	evmc::VM& _vm,
	EVMVersion _version,
	std::string const& _yulSource,
	OptimiserSettings _settings,
	bytes const& _calldata,
	bool _viaSSACFG = false
)
{
	RunResult result;
	EVMHost hostContext(_version, _vm);
	hostContext.reset();

	try
	{
		YulAssembler assembler{_version, std::nullopt, _settings, _yulSource, _viaSSACFG};
		result.bytecode = assembler.assemble();
	}
	catch (solidity::yul::StackTooDeepError const&)
	{
		result.compilationFailed = true;
		result.internalErrorMsg = "StackTooDeepError";
		return result;
	}
	catch (solidity::yul::YulException const&)
	{
		result.compilationFailed = true;
		result.internalErrorMsg = "YulException (parse/analysis/codegen failure)";
		return result;
	}
	catch (solidity::yul::YulAssertion const&)
	{
		result.compilationFailed = true;
		result.internalErrorMsg = "YulAssertion (parse/analysis failure)";
		return result;
	}

	evmc::Result deployResult = YulEvmoneUtility::deployCode(result.bytecode, hostContext, s_gasLimit);
	if (deployResult.status_code != EVMC_SUCCESS)
	{
		result.statusCode = deployResult.status_code;
		return result;
	}

	auto callMsg = YulEvmoneUtility::callMessage(deployResult.create_address, _calldata);
	callMsg.gas = s_gasLimit;
	evmc::Result callResult = hostContext.call(callMsg);
	result.statusCode = callResult.status_code;
	if (callResult.output_data && callResult.output_size > 0)
		result.output = bytes(callResult.output_data, callResult.output_data + callResult.output_size);

	// Capture logs
	result.logs.assign(hostContext.recorded_logs.begin(), hostContext.recorded_logs.end());

	// Capture storage
	for (auto const& [addr, account] : hostContext.accounts)
		if (!account.storage.empty())
			result.storage[addr] = account.storage;

	return result;
}

/// Compare logs ignoring creator address.
static bool logsEqual(
	std::vector<evmc::MockedHost::log_record> const& _a,
	std::vector<evmc::MockedHost::log_record> const& _b
)
{
	if (_a.size() != _b.size())
		return false;
	for (size_t i = 0; i < _a.size(); i++)
		if (_a[i].data != _b[i].data || _a[i].topics != _b[i].topics)
			return false;
	return true;
}

/// Filter out storage entries where current value is zero.
static std::map<evmc::address, StorageMap> filterZeroStorage(
	std::map<evmc::address, StorageMap> const& _storage
)
{
	static constexpr evmc::bytes32 zero{};
	std::map<evmc::address, StorageMap> filtered;
	for (auto const& [addr, storageMap] : _storage)
	{
		StorageMap nonZero;
		for (auto const& [key, val] : storageMap)
			if (val.current != zero)
				nonZero[key] = val;
		if (!nonZero.empty())
			filtered[addr] = std::move(nonZero);
	}
	return filtered;
}

/// Compare storage maps positionally, ignoring account addresses.
static bool storageEqual(
	std::map<evmc::address, StorageMap> const& _a,
	std::map<evmc::address, StorageMap> const& _b
)
{
	auto filtA = filterZeroStorage(_a);
	auto filtB = filterZeroStorage(_b);
	if (filtA.size() != filtB.size())
		return false;
	auto itA = filtA.begin();
	auto itB = filtB.begin();
	for (; itA != filtA.end(); ++itA, ++itB)
	{
		auto const& storageA = itA->second;
		auto const& storageB = itB->second;
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
	_out << YELLOW << "=== " << _label << " ===" << RESET << std::endl;

	if (_run.compilationFailed)
	{
		_out << "  COMPILATION FAILED";
		if (!_run.internalErrorMsg.empty())
			_out << " (" << _run.internalErrorMsg << ")";
		_out << std::endl;
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

	auto filteredStorage = filterZeroStorage(_run.storage);
	_out << "  Storage (" << filteredStorage.size() << " accounts):" << std::endl;
	for (auto const& [addr, storageMap] : filteredStorage)
	{
		_out << "    Account " << toHexString(addr) << " (" << storageMap.size() << " slots):" << std::endl;
		for (auto const& [key, val] : storageMap)
			_out << "      " << toHexString(key) << " => " << toHexString(val.current) << std::endl;
	}
	_out << std::endl;
}

/// @returns true if a mismatch was found.
static bool compareRuns(
	std::string const& _labelA,
	RunResult const& _a,
	std::string const& _labelB,
	RunResult const& _b,
	bool _quiet = false
)
{
	if (!_quiet)
		std::cout << "--- Comparing " << _labelA << " vs " << _labelB << " ---" << std::endl;

	if (_a.compilationFailed || _b.compilationFailed)
	{
		if (!_quiet)
			std::cout << "  SKIPPED (compilation failed: "
				<< _labelA << "=" << (_a.compilationFailed ? "yes" : "no") << ", "
				<< _labelB << "=" << (_b.compilationFailed ? "yes" : "no") << ")"
				<< std::endl;
		return false;
	}

	if (_a.statusCode == EVMC_OUT_OF_GAS || _b.statusCode == EVMC_OUT_OF_GAS)
	{
		if (!_quiet)
			std::cout << "  SKIPPED (out-of-gas: "
				<< _labelA << "=" << statusCodeToString(_a.statusCode) << ", "
				<< _labelB << "=" << statusCodeToString(_b.statusCode) << ")"
				<< std::endl;
		return false;
	}

	bool mismatch = false;
	auto matchStr = [](bool _match) -> std::string {
		return _match
			? std::string(GREEN) + "MATCH" + RESET
			: std::string(RED) + "DIFFER" + RESET;
	};

	// Status code
	bool statusMatch = (_a.statusCode == _b.statusCode);
	if (!statusMatch) mismatch = true;
	if (!_quiet)
		std::cout << "  Status:  " << matchStr(statusMatch)
			<< " (" << statusCodeToString(_a.statusCode) << " vs " << statusCodeToString(_b.statusCode) << ")"
			<< std::endl;

	if (_a.statusCode == EVMC_SUCCESS && _b.statusCode == EVMC_SUCCESS)
	{
		// Output
		bool outputMatch = (_a.output.size() == _b.output.size() &&
			std::memcmp(_a.output.data(), _b.output.data(), _a.output.size()) == 0);
		if (!outputMatch) mismatch = true;

		// Logs
		bool logsMatch = logsEqual(_a.logs, _b.logs);
		if (!logsMatch) mismatch = true;

		// Storage
		bool storageMatch = storageEqual(_a.storage, _b.storage);
		if (!storageMatch) mismatch = true;

		if (!_quiet)
		{
			std::cout << "  Output:  " << matchStr(outputMatch) << std::endl;
			std::cout << "  Logs:    " << matchStr(logsMatch) << std::endl;
			std::cout << "  Storage: " << matchStr(storageMatch) << std::endl;
		}
	}
	if (!_quiet)
		std::cout << std::endl;
	return mismatch;
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
	po::options_description desc("yul_debug_runner - reproduce Yul fuzzer compile/deploy/execute");
	desc.add_options()
		("help,h", "Show help")
		("input-file", po::value<std::string>(), "Yul source file")
		("output-dir", po::value<std::string>()->default_value(""), "Directory to write output files (optional)")
		("calldata", po::value<std::string>()->default_value(""), "Calldata in hex (e.g. \"a0ffba\"), passed to deployed contract")
		("optimizer-sequence", po::value<std::string>()->default_value(""), "Custom Yul optimizer step sequence (e.g. from fuzzer protobuf)")
		("optimizer-cleanup-sequence", po::value<std::string>()->default_value(""), "Custom Yul optimizer cleanup step sequence")
		("quiet,q", "Quiet mode: only print one-line summary, for use by delta debuggers")
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
		return 2;
	}

	if (vm.count("help") || !vm.count("input-file"))
	{
		std::cout << "Usage: yul_debug_runner <file.yul> [--output-dir <dir>] [--calldata <hex>] [--quiet]" << std::endl;
		std::cout << desc << std::endl;
		std::cout << std::endl;
		std::cout << "Exit codes:" << std::endl;
		std::cout << "  0 = all match (no bug)" << std::endl;
		std::cout << "  1 = mismatch found (differential bug)" << std::endl;
		std::cout << "  2 = normal compilation failure / file error" << std::endl;
		std::cout << "  3 = internal compiler error (assertion failure, crash)" << std::endl;
		return vm.count("help") ? 0 : 2;
	}

	std::string inputFile = vm["input-file"].as<std::string>();
	std::string outputDir = vm["output-dir"].as<std::string>();
	std::string calldataHex = vm["calldata"].as<std::string>();
	std::string optimizerSequence = vm["optimizer-sequence"].as<std::string>();
	std::string optimizerCleanupSequence = vm["optimizer-cleanup-sequence"].as<std::string>();
	bool quiet = vm.count("quiet") > 0;

	// Read source file
	std::ifstream ifs(inputFile);
	if (!ifs.is_open())
	{
		std::cerr << "Error: Cannot open " << inputFile << std::endl;
		return 2;
	}
	std::string yulSource{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};

	// Parse calldata
	bytes calldata;
	if (!calldataHex.empty())
	{
		calldata = util::fromHex(calldataHex);
		if (calldata.empty() && !calldataHex.empty())
		{
			std::cerr << "Error: Invalid hex calldata: " << calldataHex << std::endl;
			return 2;
		}
	}

	// Always use the latest EVM version (matching the fuzzer).
	EVMVersion version = EVMVersion::current();

	if (!quiet)
	{
		std::cout << "Source file: " << inputFile << " (" << yulSource.size() << " bytes)" << std::endl;
		std::cout << "EVM version: " << version.name() << " (latest, hardcoded)" << std::endl;
		if (!calldataHex.empty())
			std::cout << "Calldata: " << calldataHex << std::endl;
		if (!optimizerSequence.empty())
			std::cout << "Optimizer sequence: " << optimizerSequence << std::endl;
		if (!optimizerCleanupSequence.empty())
			std::cout << "Optimizer cleanup sequence: " << optimizerCleanupSequence << std::endl;
		std::cout << std::endl;
	}

	// Load evmone VM
	evmc::VM& evmVM = EVMHost::getVM("libevmone.so");
	if (!evmVM)
	{
		std::cerr << "Error: Could not load evmone VM. Set LD_LIBRARY_PATH to include evmone lib directory." << std::endl;
		return 2;
	}

	// Run 4 configurations: unoptimized, optimized (legacy), optimized (SSACFG),
	// optimized (legacy, no stack alloc)
	struct Config
	{
		std::string label;
		OptimiserSettings settings;
		bool viaSSACFG;
	};

	OptimiserSettings settingsNoOpt = OptimiserSettings::full();
	settingsNoOpt.runYulOptimiser = false;
	settingsNoOpt.optimizeStackAllocation = false;

	OptimiserSettings settingsOpt = OptimiserSettings::full();
	settingsOpt.runYulOptimiser = true;
	settingsOpt.optimizeStackAllocation = true;
	if (!optimizerSequence.empty())
		settingsOpt.yulOptimiserSteps = optimizerSequence;
	if (!optimizerCleanupSequence.empty())
		settingsOpt.yulOptimiserCleanupSteps = optimizerCleanupSequence;

	OptimiserSettings settingsOptNoStackAlloc = OptimiserSettings::full();
	settingsOptNoStackAlloc.runYulOptimiser = true;
	settingsOptNoStackAlloc.optimizeStackAllocation = false;
	if (!optimizerSequence.empty())
		settingsOptNoStackAlloc.yulOptimiserSteps = optimizerSequence;
	if (!optimizerCleanupSequence.empty())
		settingsOptNoStackAlloc.yulOptimiserCleanupSteps = optimizerCleanupSequence;

	std::vector<Config> configs = {
		{"unoptimized", settingsNoOpt, false},
		{"optimized_legacy", settingsOpt, false},
		{"optimized_ssacfg", settingsOpt, true},
		{"optimized_legacy_no_stack_alloc", settingsOptNoStackAlloc, false},
	};

	std::vector<RunResult> results;
	for (auto const& config : configs)
	{
		if (!quiet)
			std::cout << "Running: " << config.label << "..." << std::endl;
		try
		{
			results.push_back(runYulOnce(evmVM, version, yulSource, config.settings, calldata, config.viaSSACFG));
		}
		catch (solidity::yul::StackTooDeepError const&)
		{
			if (!quiet)
				std::cout << "  StackTooDeepError" << std::endl;
			RunResult r;
			r.compilationFailed = true;
			r.internalErrorMsg = "StackTooDeepError";
			results.push_back(std::move(r));
		}
		catch (std::exception const& e)
		{
			if (!quiet)
				std::cout << "  Exception: " << e.what() << std::endl;
			RunResult r;
			r.compilationFailed = true;
			r.internalError = true;
			r.internalErrorMsg = e.what();
			results.push_back(std::move(r));
		}
	}

	if (!quiet)
	{
		std::cout << std::endl;

		// Print all results
		for (size_t i = 0; i < configs.size(); i++)
			printRunResult(configs[i].label, results[i], std::cout);
	}

	// Run differential comparisons
	bool anyMismatch = false;
	if (!quiet)
		std::cout << YELLOW << "========== DIFFERENTIAL COMPARISONS ==========" << RESET << std::endl << std::endl;

	// unoptimized vs optimized (legacy) — same as yul_proto_ossfuzz_evmone
	anyMismatch |= compareRuns(configs[0].label, results[0], configs[1].label, results[1], quiet);
	// unoptimized vs optimized (SSACFG) — same as yul_proto_ossfuzz_evmone_ssacfg
	anyMismatch |= compareRuns(configs[0].label, results[0], configs[2].label, results[2], quiet);
	// optimized legacy vs optimized SSACFG — cross-backend
	anyMismatch |= compareRuns(configs[1].label, results[1], configs[2].label, results[2], quiet);
	// optimized (no stack alloc) vs optimized (stack alloc) — same as _check_stack_alloc fuzzer
	anyMismatch |= compareRuns(configs[3].label, results[3], configs[1].label, results[1], quiet);

	if (!quiet)
	{
		// Print outputs
		std::cout << YELLOW << "========== OUTPUTS ==========" << RESET << std::endl << std::endl;
		for (size_t i = 0; i < configs.size(); i++)
		{
			std::cout << "--- " << configs[i].label << " ---" << std::endl;
			if (results[i].compilationFailed)
				std::cout << "  COMPILATION FAILED" << std::endl;
			else
			{
				std::cout << "  Status: " << statusCodeToString(results[i].statusCode) << std::endl;
				std::cout << "  Output (" << results[i].output.size() << " bytes): "
					<< toHexString(results[i].output) << std::endl;
			}
			std::cout << std::endl;
		}

		// Print logs
		std::cout << YELLOW << "========== LOGS ==========" << RESET << std::endl;
		std::cout << "NOTE: Creator addresses differ across configs because different bytecodes" << std::endl;
		std::cout << "produce different CREATE/CREATE2 addresses. This is expected and not a bug." << std::endl;
		std::cout << std::endl;
		for (size_t i = 0; i < configs.size(); i++)
		{
			std::cout << "--- " << configs[i].label << " ---" << std::endl;
			if (results[i].compilationFailed)
				std::cout << "  COMPILATION FAILED" << std::endl;
			else if (results[i].logs.empty())
				std::cout << "  (no logs)" << std::endl;
			else
			{
				for (size_t j = 0; j < results[i].logs.size(); j++)
				{
					auto const& log = results[i].logs[j];
					std::cout << "  Log[" << j << "]:" << std::endl;
					std::cout << "    Creator: " << toHexString(log.creator) << std::endl;
					std::cout << "    Data (" << log.data.size() << " bytes): ";
					std::ostringstream dataSS;
					for (uint8_t b : log.data)
						dataSS << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
					std::cout << dataSS.str() << std::endl;
					std::cout << "    Topics (" << log.topics.size() << "):" << std::endl;
					for (size_t k = 0; k < log.topics.size(); k++)
						std::cout << "      [" << k << "]: " << toHexString(log.topics[k]) << std::endl;
				}
			}
			std::cout << std::endl;
		}

		// Print storage
		std::cout << YELLOW << "========== STORAGE ==========" << RESET << std::endl;
		std::cout << "NOTE: Account addresses differ across configs because different bytecodes" << std::endl;
		std::cout << "produce different CREATE/CREATE2 addresses. This is expected and not a bug." << std::endl;
		std::cout << "NOTE: Slots with value zero are hidden (equivalent to unwritten in the EVM)." << std::endl;
		std::cout << std::endl;
		for (size_t i = 0; i < configs.size(); i++)
		{
			std::cout << "--- " << configs[i].label << " ---" << std::endl;
			if (results[i].compilationFailed)
				std::cout << "  COMPILATION FAILED" << std::endl;
			else
			{
				auto filtered = filterZeroStorage(results[i].storage);
				if (filtered.empty())
					std::cout << "  (no storage)" << std::endl;
				else
				{
					for (auto const& [addr, storageMap] : filtered)
					{
						std::cout << "  Account " << toHexString(addr) << " (" << storageMap.size() << " slots):" << std::endl;
						for (auto const& [key, val] : storageMap)
							std::cout << "    " << toHexString(key) << " => " << toHexString(val.current) << std::endl;
					}
				}
			}
			std::cout << std::endl;
		}

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
	}

	// Check if any config hit an internal error or all failed to compile
	bool anyInternalError = false;
	bool allCompilationFailed = true;
	for (auto const& r : results)
	{
		if (r.internalError)
			anyInternalError = true;
		if (!r.compilationFailed)
			allCompilationFailed = false;
	}

	// Exit codes: 0 = all match, 1 = mismatch, 2 = compilation failure, 3 = internal error
	int exitCode;
	std::string summary;
	if (anyInternalError)
	{
		exitCode = 3;
		summary = "INTERNAL_ERROR";
	}
	else if (anyMismatch)
	{
		exitCode = 1;
		summary = "MISMATCH";
	}
	else if (allCompilationFailed)
	{
		exitCode = 2;
		summary = "COMPILATION_FAILED";
	}
	else
	{
		exitCode = 0;
		summary = "OK";
	}

	if (quiet)
		std::cout << summary << std::endl;

	return exitCode;
}
