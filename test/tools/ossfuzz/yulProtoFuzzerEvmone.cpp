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
 * Yul proto fuzzer with evmone-based differential testing.
 *
 * Generates Yul code from protobuf, compiles it twice, deploys both versions
 * on evmone, executes with the same calldata, and compares output, logs, and
 * storage — similar to sol_proto_ossfuzz_evmone but for Yul.
 *
 * Two modes (controlled by compile definition):
 * - Default (yul_proto_ossfuzz_evmone): unoptimized vs optimized, both legacy codegen
 * - FUZZER_MODE_SSACFG (yul_proto_ossfuzz_evmone_ssacfg): unoptimized legacy vs
 *   optimized SSA CFG codegen
 */

#include <test/tools/ossfuzz/yulProto.pb.h>
#include <test/tools/ossfuzz/protoToYul.h>

#include <test/EVMHost.h>

#include <test/tools/ossfuzz/YulEvmoneInterface.h>

#include <libyul/Exceptions.h>

#include <libsolidity/interface/OptimiserSettings.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/EVMVersion.h>

#include <evmone/evmone.h>

#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>
#include <cstring>
#include <map>

using namespace solidity;
using namespace solidity::test;
using namespace solidity::test::fuzzer;
using namespace solidity::yul;
using namespace solidity::yul::test;
using namespace solidity::yul::test::yul_fuzzer;
using namespace solidity::langutil;
using namespace solidity::frontend;

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

/// Fuzzer mode selection (controlled by compile definitions):
/// - Default: unoptimized vs optimized (both legacy codegen)
/// - FUZZER_MODE_SSACFG: unoptimized (legacy) vs optimized (SSA CFG codegen)
#ifdef FUZZER_MODE_SSACFG
static constexpr bool s_modeSSACFG = true;
#else
static constexpr bool s_modeSSACFG = false;
#endif

/// Gas limit for EVM execution — bounds runtime and memory usage
/// (prevents LOG/CALL spam from causing OOM or timeouts).
static constexpr int64_t s_gasLimit = 400000;

/// Result of a single compile-deploy-execute run on evmone.
struct RunResult
{
	evmc::Result result;
	std::vector<evmc::MockedHost::log_record> logs;
	std::map<evmc::address, StorageMap> storage;
};

namespace
{

/// Maps a sequence of uint32 values to a Yul optimizer step abbreviation string.
std::string buildOptimizerSequence(google::protobuf::RepeatedField<google::protobuf::uint32> const& _steps)
{
	static std::string const validChars = "flcCUnDEvejsxIOoighFTLMrSmVatpud";

	if (_steps.empty())
		return OptimiserSettings::DefaultYulOptimiserSteps;

	std::string seq;
	seq.reserve(static_cast<size_t>(_steps.size()) + 2);
	for (auto const s: _steps)
		seq += validChars[s % validChars.size()];

	if (_steps.size() >= 4)
	{
		size_t const n = seq.size();
		size_t const lo = n / 4;
		size_t const hi = (3 * n) / 4;
		seq = seq.substr(0, lo) + "[" + seq.substr(lo, hi - lo) + "]" + seq.substr(hi);
	}

	return seq;
}

/// Compare two log records for equality (ignoring creator address,
/// which differs across optimization levels because different bytecodes
/// produce different CREATE/CREATE2 addresses).
bool logsEqual(
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
std::map<evmc::address, StorageMap> filterZeroStorage(
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
/// Ignores account addresses because different bytecodes produce different
/// CREATE/CREATE2 addresses. Instead, compares accounts positionally.
bool storageEqual(
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

/// Compile Yul source, deploy on evmone, execute with calldata, return results.
RunResult runYulOnce(
	EVMVersion _version,
	std::string const& _yulSource,
	OptimiserSettings _settings,
	bytes const& _calldata,
	bool _viaSSACFG = false
)
{
	EVMHost hostContext(_version, evmone);
	hostContext.reset();

	bytes byteCode;
	try
	{
		YulAssembler assembler{_version, std::nullopt, _settings, _yulSource, _viaSSACFG};
		byteCode = assembler.assemble();
	}
	catch (solidity::yul::StackTooDeepError const&)
	{
		return RunResult{evmc::Result{EVMC_INTERNAL_ERROR}, {}, {}};
	}
	catch (solidity::yul::YulException const&)
	{
		// Parse/analysis/codegen failure — skip this input.
		return RunResult{evmc::Result{EVMC_INTERNAL_ERROR}, {}, {}};
	}
	catch (solidity::yul::YulAssertion const&)
	{
		// Parse/analysis assertion failure — skip this input.
		return RunResult{evmc::Result{EVMC_INTERNAL_ERROR}, {}, {}};
	}

	evmc::Result deployResult = YulEvmoneUtility::deployCode(byteCode, hostContext, s_gasLimit);
	if (deployResult.status_code != EVMC_SUCCESS)
		return RunResult{std::move(deployResult), {}, {}};

	auto callMsg = YulEvmoneUtility::callMessage(deployResult.create_address, _calldata);
	callMsg.gas = s_gasLimit;
	evmc::Result callResult = hostContext.call(callMsg);

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

	return RunResult{std::move(callResult), std::move(logs), std::move(storage)};
}

} // anonymous namespace

DEFINE_PROTO_FUZZER(Program const& _input)
{
	// Subobjects cause issues with deployment — skip them.
	if (_input.has_obj())
		return;

	// filterStatefulInstructions=false: keep sstore/tstore/log — we compare them.
	// filterOptimizationNoise=true: filter datasize/dataoffset that inherently differ.
	bool filterStatefulInstructions = false;
	bool filterOptimizationNoise = true;
	ProtoConverter converter(
		filterStatefulInstructions,
		filterOptimizationNoise
	);
	std::string yul_source = converter.programToString(_input);
	// Always use the latest EVM version for maximum feature coverage.
	EVMVersion version = EVMVersion::current();
	auto calldata = converter.calldata();

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		std::ofstream of(dump_path);
		of.write(yul_source.data(), static_cast<std::streamsize>(yul_source.size()));
	}

	YulStringRepository::reset();

	// --- Run A: unoptimized (legacy codegen) ---
	OptimiserSettings settingsNoOpt = OptimiserSettings::full();
	settingsNoOpt.runYulOptimiser = false;
	settingsNoOpt.optimizeStackAllocation = false;

	auto runA = runYulOnce(version, yul_source, settingsNoOpt, calldata, /*viaSSACFG=*/false);

	// Bail on deployment failure or serious call errors
	if (runA.result.status_code != EVMC_SUCCESS &&
		runA.result.status_code != EVMC_REVERT)
		return;

	// --- Run B: optimized ---
	OptimiserSettings settingsOpt = OptimiserSettings::full();
	settingsOpt.runYulOptimiser = true;
	settingsOpt.optimizeStackAllocation = true;

	// Use custom optimizer sequence if provided in proto
	if (_input.optimiser_seq_size() > 0)
	{
		settingsOpt.yulOptimiserSteps = buildOptimizerSequence(_input.optimiser_seq());
		settingsOpt.yulOptimiserCleanupSteps = _input.optimiser_cleanup_seq_size() > 0
			? buildOptimizerSequence(_input.optimiser_cleanup_seq())
			: std::string(OptimiserSettings::DefaultYulOptimiserCleanupSteps);
	}

	// In SSACFG mode: run B uses the SSA CFG codegen backend.
	// In default mode: run B uses legacy codegen (same as run A).
	auto runB = runYulOnce(version, yul_source, settingsOpt, calldata, /*viaSSACFG=*/s_modeSSACFG);

	// Skip comparison if either run hit gas-related or serious errors
	bool gasRelated =
		runA.result.status_code == EVMC_OUT_OF_GAS ||
		runB.result.status_code == EVMC_OUT_OF_GAS;
	if (gasRelated)
		return;

	if (YulEvmoneUtility::seriousCallError(runA.result.status_code) ||
		YulEvmoneUtility::seriousCallError(runB.result.status_code))
		return;

	std::string const modeLabel = s_modeSSACFG
		? "Yul evmone fuzzer (SSACFG mode): unoptimized legacy vs optimized SSACFG"
		: "Yul evmone fuzzer: optimized vs non-optimized";

	// Compare status codes
	solAssert(
		runA.result.status_code == runB.result.status_code,
		modeLabel + " status code differs (A=" +
		std::to_string(runA.result.status_code) + " B=" +
		std::to_string(runB.result.status_code) + ")"
	);

	// Compare output, logs, and storage when both succeeded
	if (runA.result.status_code == EVMC_SUCCESS && runB.result.status_code == EVMC_SUCCESS)
	{
		solAssert(
			runA.result.output_size == runB.result.output_size &&
			std::memcmp(runA.result.output_data, runB.result.output_data, runA.result.output_size) == 0,
			modeLabel + " output differs"
		);
		solAssert(
			logsEqual(runA.logs, runB.logs),
			modeLabel + " logs differ"
		);
		solAssert(
			storageEqual(runA.storage, runB.storage),
			modeLabel + " storage differs"
		);
	}
}
