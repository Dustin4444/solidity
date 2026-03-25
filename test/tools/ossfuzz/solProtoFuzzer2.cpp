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
static constexpr int64_t s_gasLimit = 1000000;

/// Fuzzer mode selection (controlled by compile definitions):
/// - Default: unoptimized vs optimized (same viaIR flag)
/// - FUZZER_MODE_VIAIR: optimized vs optimized-viaIR
#ifdef FUZZER_MODE_VIAIR
static constexpr bool s_modeViaIR = true;
#else
static constexpr bool s_modeViaIR = false;
#endif

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

/// Compare two log records for equality (ignoring creator address,
/// which differs across optimization levels because different bytecodes
/// produce different CREATE/CREATE2 addresses).
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
/// In the EVM, an unwritten slot reads as zero, so a slot explicitly
/// set to zero is semantically identical to one that was never written.
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

/// Compare storage maps for equality (comparing current values only).
/// Ignores account addresses because different bytecodes (optimized vs
/// non-optimized) produce different CREATE/CREATE2 addresses. Instead,
/// compares accounts positionally (by sorted address order).
/// Slots with value zero are filtered out (equivalent to unwritten).
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
	if (sol_source.size() > 3000)
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
		if (s_modeViaIR)
		{
			// Mode: optimized (legacy) vs optimized (viaIR)
			auto runA = runOnce(version, source, OptimiserSettings::standard(), false, extraCalldataHex);
			if (runA.result.status_code != EVMC_SUCCESS)
				return;

			auto runB = runOnce(version, source, OptimiserSettings::standard(), true, extraCalldataHex);

			bool gasRelated =
				runA.result.status_code == EVMC_OUT_OF_GAS ||
				runB.result.status_code == EVMC_OUT_OF_GAS;

			if (!gasRelated && runB.result.status_code == EVMC_SUCCESS)
			{
				solAssert(
					runA.result.output_size == runB.result.output_size &&
					std::memcmp(runA.result.output_data, runB.result.output_data, runA.result.output_size) == 0,
					"Sol proto2 fuzzer (viaIR mode): optimized legacy vs optimized viaIR output differs"
				);
				solAssert(
					logsEqual(runA.logs, runB.logs),
					"Sol proto2 fuzzer (viaIR mode): optimized legacy vs optimized viaIR logs differ"
				);
				solAssert(
					storageEqual(runA.storage, runB.storage),
					"Sol proto2 fuzzer (viaIR mode): optimized legacy vs optimized viaIR storage differs"
				);
			}
		}
		else
		{
			// Mode: unoptimized vs optimized (same viaIR flag)
			auto runNoOpt = runOnce(version, source, OptimiserSettings::minimal(), viaIR, extraCalldataHex);

			if (runNoOpt.result.status_code != EVMC_SUCCESS)
				return;

			auto runOpt = runOnce(version, source, OptimiserSettings::standard(), viaIR, extraCalldataHex);

			bool gasRelated =
				runNoOpt.result.status_code == EVMC_OUT_OF_GAS ||
				runOpt.result.status_code == EVMC_OUT_OF_GAS;

			if (!gasRelated)
			{
				solAssert(
					runNoOpt.result.status_code == runOpt.result.status_code,
					"Sol proto2 fuzzer: status code differs (noOpt=" +
					std::to_string(runNoOpt.result.status_code) + " opt=" +
					std::to_string(runOpt.result.status_code) + ")"
				);

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
		}
	}
	catch (evmasm::StackTooDeepException const&)
	{
		// Stack-too-deep in legacy codegen is expected for some inputs.
	}
}
