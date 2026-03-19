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

#include <test/tools/ossfuzz/protoToSol2.h>
#include <test/tools/ossfuzz/SolidityEvmoneInterface.h>
#include <test/tools/ossfuzz/sol2Proto.pb.h>

#include <test/EVMHost.h>

#include <libevmasm/Exceptions.h>

#include <evmone/evmone.h>
#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>
#include <cstring>
#include <map>

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

using namespace solidity::test::fuzzer;
using namespace solidity::test::sol2protofuzzer;
using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::test;
using namespace solidity::util;

/// Result of a single compile-deploy-execute run, including EVM result,
/// recorded logs, and storage state for differential comparison.
struct RunResult
{
	evmc::Result result;
	std::vector<evmc::MockedHost::log_record> logs;
	std::map<evmc::address, StorageMap> storage;
};

/// Gas limit for EVM execution — low enough to keep fuzzing fast,
/// high enough to deploy and run simple contracts.
static constexpr int64_t s_gasLimit = 100000;

/// Helper: compile, deploy, and execute a test contract.
/// Returns RunResult with evmc::Result, logs, and storage.
static RunResult runOnce(
	langutil::EVMVersion _version,
	StringMap const& _source,
	OptimiserSettings _optimiserSettings,
	bool _viaIR,
	std::string const& _extraCalldataHex = {}
)
{
	EVMHost hostContext(_version, evmone);
	std::string contractName = "C";
	std::string methodName = "test()";
	CompilerInput cInput(
		_version,
		_source,
		contractName,
		_optimiserSettings,
		{},
		/*debugFailure=*/false,
		_viaIR
	);
	EvmoneUtility evmoneUtil(
		hostContext,
		cInput,
		contractName,
		/*libraryName=*/"",
		methodName,
		s_gasLimit
	);
	evmc::Result result = evmoneUtil.compileDeployAndExecute({}, _extraCalldataHex);

	// Capture logs
	std::vector<evmc::MockedHost::log_record> logs(
		hostContext.recorded_logs.begin(),
		hostContext.recorded_logs.end()
	);

	// Capture storage for all accounts
	std::map<evmc::address, StorageMap> storage;
	for (auto const& [addr, account] : hostContext.accounts)
		if (!account.storage.empty())
			storage[addr] = account.storage;

	return RunResult{std::move(result), std::move(logs), std::move(storage)};
}

/// Compare two log records for equality.
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

/// Compare storage maps for equality (comparing current values only).
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

DEFINE_PROTO_FUZZER(Program const& _input)
{
	ProtoConverter converter;
	std::string sol_source = converter.protoToSolidity(_input);

	if (char const* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		std::ofstream of(dump_path);
		of.write(sol_source.data(), static_cast<std::streamsize>(sol_source.size()));
	}

	if (char const* dump_path = getenv("SOL_DEBUG_FILE"))
	{
		sol_source.clear();
		std::ifstream ifstr(dump_path);
		sol_source = {
			std::istreambuf_iterator<char>(ifstr),
			std::istreambuf_iterator<char>()
		};
		std::cout << sol_source << std::endl;
	}

	// Skip overly large sources — they compile slowly and reduce throughput
	if (sol_source.size() > 20000)
		return;

	// Always fuzz the latest EVM version to maximize feature coverage
	// (transient storage, EOF, etc.). Do NOT parameterize this.
	langutil::EVMVersion version = langutil::EVMVersion::current();
	bool viaIR = _input.via_ir();
	StringMap source({{"test.sol", sol_source}});

	// Convert proto calldata bytes to hex for appending after the method selector
	std::string extraCalldataHex;
	if (_input.has_calldata_data())
	{
		bytes calldataBytes(_input.calldata_data().begin(), _input.calldata_data().end());
		extraCalldataHex = toHex(calldataBytes);
	}

	try
	{
		// Run 1: without optimization
		auto runNoOpt = runOnce(version, source, OptimiserSettings::minimal(), viaIR, extraCalldataHex);

		// Skip second compilation if first failed (compilation error,
		// deploy revert, etc.) — no point optimizing broken code.
		if (runNoOpt.result.status_code != EVMC_SUCCESS)
			return;

		// Run 2: with optimization
		auto runOpt = runOnce(version, source, OptimiserSettings::standard(), viaIR, extraCalldataHex);

		// Skip differential checks if either run hit out-of-gas.
		// With a tight gas limit, optimized code may use less gas and
		// succeed where unoptimized doesn't (or vice versa). That's
		// expected and not a bug.
		bool gasRelated =
			runNoOpt.result.status_code == EVMC_OUT_OF_GAS ||
			runOpt.result.status_code == EVMC_OUT_OF_GAS;

		if (!gasRelated)
		{
			// Status codes must match (same input, same gas, no gas queries).
			solAssert(
				runNoOpt.result.status_code == runOpt.result.status_code,
				"Sol proto2 fuzzer: status code differs (noOpt=" +
				std::to_string(runNoOpt.result.status_code) + " opt=" +
				std::to_string(runOpt.result.status_code) + ")"
			);

			// If both succeed, outputs, logs, and storage must also match.
			if (runNoOpt.result.status_code == EVMC_SUCCESS && runOpt.result.status_code == EVMC_SUCCESS)
			{
				solAssert(
					runNoOpt.result.output_size == runOpt.result.output_size &&
					std::memcmp(runNoOpt.result.output_data, runOpt.result.output_data, runNoOpt.result.output_size) == 0,
					"Sol proto2 fuzzer: optimized vs non-optimized output differs"
				);
				solAssert(
					logsEqual(runNoOpt.logs, runOpt.logs),
					"Sol proto2 fuzzer: optimized vs non-optimized logs differ"
				);
				solAssert(
					storageEqual(runNoOpt.storage, runOpt.storage),
					"Sol proto2 fuzzer: optimized vs non-optimized storage differs"
				);
			}
		}

		// Run 3: same optimization but with opposite viaIR flag.
		// Comparing viaIR vs legacy catches IR codegen bugs.
		// Only attempt if first run succeeded (source is valid).
		if (runNoOpt.result.status_code == EVMC_SUCCESS)
		{
			try
			{
				auto runAlt = runOnce(version, source, OptimiserSettings::minimal(), !viaIR, extraCalldataHex);
				// Skip if either hit out-of-gas (different codegen paths
				// have different gas costs).
				if (runAlt.result.status_code != EVMC_OUT_OF_GAS &&
					runAlt.result.status_code == EVMC_SUCCESS)
				{
					solAssert(
						runNoOpt.result.output_size == runAlt.result.output_size &&
						std::memcmp(runNoOpt.result.output_data, runAlt.result.output_data, runNoOpt.result.output_size) == 0,
						"Sol proto2 fuzzer: viaIR=" + std::string(viaIR ? "true" : "false") +
						" vs viaIR=" + std::string(!viaIR ? "true" : "false") + " output differs"
					);
					solAssert(
						logsEqual(runNoOpt.logs, runAlt.logs),
						"Sol proto2 fuzzer: viaIR=" + std::string(viaIR ? "true" : "false") +
						" vs viaIR=" + std::string(!viaIR ? "true" : "false") + " logs differ"
					);
					solAssert(
						storageEqual(runNoOpt.storage, runAlt.storage),
						"Sol proto2 fuzzer: viaIR=" + std::string(viaIR ? "true" : "false") +
						" vs viaIR=" + std::string(!viaIR ? "true" : "false") + " storage differs"
					);
				}
			}
			catch (evmasm::StackTooDeepException const&)
			{
				// Legacy codegen may hit stack-too-deep for inputs that work with viaIR
			}
		}
	}
	catch (evmasm::StackTooDeepException const&)
	{
		// Stack-too-deep in legacy codegen is expected for some inputs.
	}
}
