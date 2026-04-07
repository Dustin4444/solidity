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
#include <liblangutil/Exceptions.h>

#include <libyul/YulString.h>

#include <evmone/evmone.h>
#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>
#include <cstring>
#include <map>
#include <unordered_map>

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

using namespace solidity::test::fuzzer;
using namespace solidity::test::sol2protofuzzer;
using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::test;
using namespace solidity::util;

/// Result of a single compile-deploy-execute run, including EVM result,
/// recorded logs, and storage state for differential comparison.
/// Transient storage map type: slot → value (no StorageValue wrapper).
using TransientStorageMap = std::unordered_map<evmc::bytes32, evmc::bytes32>;

struct RunResult
{
	evmc::Result result;
	bool subCallOutOfGas = false;
	std::vector<evmc::MockedHost::log_record> logs;
	std::map<evmc::address, StorageMap> storage;
	std::map<evmc::address, TransientStorageMap> transientStorage;
	/// Contract creation order: addresses in the order they were deployed (CREATE/CREATE2).
	std::vector<evmc::address> contractCreationOrder;
};

/// Gas limit for EVM execution — low enough to keep fuzzing fast,
/// high enough to deploy and run simple contracts.
static constexpr int64_t s_gasLimit = 1000000;

/// Fuzzer mode selection (controlled by compile definitions):
/// - Default: unoptimized vs optimized (same viaIR flag)
/// - FUZZER_MODE_VIAIR: unoptimized (legacy) vs optimized (viaIR)
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
	// Give the sender (tx.origin) some initial balance so that value
	// transfers in .call{value:...}() work during testing.
	hostContext.accounts[hostContext.tx_context.tx_origin].set_balance(0xffffffff);
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
	bool subCallOOG = hostContext.m_subCallOutOfGas;

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

	// Capture transient storage for all accounts
	std::map<evmc::address, TransientStorageMap> transientStorage;
	for (auto const& [addr, account] : hostContext.accounts)
		if (!account.transient_storage.empty())
			transientStorage[addr] = account.transient_storage;

	std::vector<evmc::address> contractCreationOrder = hostContext.m_contractCreationOrder;

	return RunResult{std::move(result), subCallOOG, std::move(logs), std::move(storage), std::move(transientStorage), std::move(contractCreationOrder)};
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

/// Filter out transient storage entries where value is zero.
static std::map<evmc::address, TransientStorageMap> filterZeroTransientStorage(
	std::map<evmc::address, TransientStorageMap> const& _storage
)
{
	static constexpr evmc::bytes32 zero{};
	std::map<evmc::address, TransientStorageMap> filtered;
	for (auto const& [addr, storageMap] : _storage)
	{
		TransientStorageMap nonZero;
		for (auto const& [key, val] : storageMap)
			if (val != zero)
				nonZero[key] = val;
		if (!nonZero.empty())
			filtered[addr] = std::move(nonZero);
	}
	return filtered;
}

/// Compare transient storage maps for equality (positionally, ignoring addresses).
static bool transientStorageEqual(
	std::map<evmc::address, TransientStorageMap> const& _a,
	std::map<evmc::address, TransientStorageMap> const& _b
)
{
	auto filtA = filterZeroTransientStorage(_a);
	auto filtB = filterZeroTransientStorage(_b);
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
			if (valA != jt->second)
				return false;
		}
	}
	return true;
}

/// Build a map from creation-order index to non-zero storage content.
/// Accounts not found in creationOrder (e.g. precompiles, sender) are
/// assigned indices starting after the last creation-order entry.
static std::map<size_t, StorageMap> normalizeStorageByCreationOrder(
	std::map<evmc::address, StorageMap> const& _storage,
	std::vector<evmc::address> const& _creationOrder
)
{
	auto filtered = filterZeroStorage(_storage);
	std::map<size_t, StorageMap> result;
	size_t unknownIdx = _creationOrder.size();
	for (auto const& [addr, storageMap] : filtered)
	{
		auto it = std::find(_creationOrder.begin(), _creationOrder.end(), addr);
		size_t idx = (it != _creationOrder.end())
			? static_cast<size_t>(std::distance(_creationOrder.begin(), it))
			: unknownIdx++;
		result[idx] = storageMap;
	}
	return result;
}

/// Compare storage maps for equality (comparing current values only).
/// Accounts are matched by CONTRACT CREATION ORDER (not by address), so that
/// different bytecodes producing different CREATE/CREATE2 addresses don't cause
/// false positives. Slots with value zero are filtered out (equivalent to unwritten).
static bool storageEqual(
	std::map<evmc::address, StorageMap> const& _a,
	std::vector<evmc::address> const& _creationOrderA,
	std::map<evmc::address, StorageMap> const& _b,
	std::vector<evmc::address> const& _creationOrderB
)
{
	auto normA = normalizeStorageByCreationOrder(_a, _creationOrderA);
	auto normB = normalizeStorageByCreationOrder(_b, _creationOrderB);
	if (normA.size() != normB.size())
		return false;
	for (auto const& [idx, storageA] : normA)
	{
		auto jt = normB.find(idx);
		if (jt == normB.end())
			return false;
		auto const& storageB = jt->second;
		if (storageA.size() != storageB.size())
			return false;
		for (auto const& [key, valA] : storageA)
		{
			auto kt = storageB.find(key);
			if (kt == storageB.end())
				return false;
			if (valA.current != kt->second.current)
				return false;
		}
	}
	return true;
}

/// Maps a sequence of uint32 values to a Yul optimizer step abbreviation string.
/// Capped at 64 steps to prevent pathological sequences that cause optimizer timeouts.
/// Bracket placement is derived from the input data for diverse iteration patterns.
static std::string buildOptimizerSequence(google::protobuf::RepeatedField<google::protobuf::uint32> const& _steps)
{
	// TODO: Remove this early return to re-enable random optimization sequences.
	// Currently disabled to use only the default sequence.
	(void)_steps;
	return OptimiserSettings::DefaultYulOptimiserSteps;

	static std::string const validChars = "flcCUnDEvejsxIOoighFTLMrSmVatpud";
	static constexpr size_t maxSteps = 64;

	if (_steps.empty())
		return OptimiserSettings::DefaultYulOptimiserSteps;

	size_t const numSteps = std::min(static_cast<size_t>(_steps.size()), maxSteps);
	std::string seq;
	seq.reserve(numSteps + 4);
	for (size_t i = 0; i < numSteps; ++i)
		seq += validChars[_steps[static_cast<int>(i)] % validChars.size()];

	// Use first two step values to control bracket placement diversity.
	// This allows the fuzzer to explore: no brackets, brackets at various positions.
	if (seq.size() >= 3)
	{
		uint32_t control = _steps[0];
		size_t const n = seq.size();
		// 1 in 4 chance of no brackets at all
		if (control % 4 != 0)
		{
			// Derive bracket positions from input data
			size_t lo = (_steps.size() >= 2 ? _steps[1] : control) % n;
			size_t hi = (_steps.size() >= 3 ? _steps[2] : control / 7) % n;
			if (lo > hi)
				std::swap(lo, hi);
			// Ensure at least one step inside brackets
			if (hi <= lo)
				hi = lo + 1;
			if (hi > n)
				hi = n;
			seq = seq.substr(0, lo) + "[" + seq.substr(lo, hi - lo) + "]" + seq.substr(hi);
		}
	}

	return seq;
}

DEFINE_PROTO_FUZZER(Program const& _input)
{
	yul::YulStringRepository::reset();

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
		// Build custom optimizer sequences from proto if provided
		std::string optimizerSeq = _input.optimiser_seq_size() > 0
			? buildOptimizerSequence(_input.optimiser_seq())
			: std::string(OptimiserSettings::DefaultYulOptimiserSteps);
		std::string optimizerCleanupSeq = _input.optimiser_cleanup_seq_size() > 0
			? buildOptimizerSequence(_input.optimiser_cleanup_seq())
			: std::string(OptimiserSettings::DefaultYulOptimiserCleanupSteps);

		// Choose settings for the two runs based on mode
		OptimiserSettings settingsA, settingsB;
		bool viaIR_A, viaIR_B;
		std::string modeLabel;
		if (s_modeViaIR)
		{
			settingsA = OptimiserSettings::minimal();
			settingsB = OptimiserSettings::standard();
			settingsB.yulOptimiserSteps = optimizerSeq;
			settingsB.yulOptimiserCleanupSteps = optimizerCleanupSeq;
			viaIR_A = false;
			viaIR_B = true;
			modeLabel = "Sol proto2 fuzzer (viaIR mode)";
		}
		else
		{
			settingsA = OptimiserSettings::minimal();
			settingsB = OptimiserSettings::standard();
			settingsB.yulOptimiserSteps = optimizerSeq;
			settingsB.yulOptimiserCleanupSteps = optimizerCleanupSeq;
			viaIR_A = viaIR;
			viaIR_B = viaIR;
			modeLabel = "Sol proto2 fuzzer";
		}

		// Always run both configurations
		auto runA = runOnce(version, source, settingsA, viaIR_A, extraCalldataHex);
		auto runB = runOnce(version, source, settingsB, viaIR_B, extraCalldataHex);

		// Skip on deployment failure (neither run produced a callable contract)
		if (runA.result.status_code != EVMC_SUCCESS &&
			runA.result.status_code != EVMC_REVERT)
			return;
		if (runB.result.status_code != EVMC_SUCCESS &&
			runB.result.status_code != EVMC_REVERT)
			return;

		// Skip on gas-related differences (legitimate across optimization levels)
		bool gasRelated =
			runA.result.status_code == EVMC_OUT_OF_GAS ||
			runB.result.status_code == EVMC_OUT_OF_GAS ||
			runA.subCallOutOfGas ||
			runB.subCallOutOfGas;
		if (gasRelated)
			return;

		// Compare status codes (catches success-vs-revert mismatches)
		solAssert(
			runA.result.status_code == runB.result.status_code,
			modeLabel + ": status code differs (A=" +
			std::to_string(runA.result.status_code) + " B=" +
			std::to_string(runB.result.status_code) + ")"
		);

		// Compare output/logs/storage when both succeeded
		if (runA.result.status_code == EVMC_SUCCESS && runB.result.status_code == EVMC_SUCCESS)
		{
			solAssert(
				runA.result.output_size == runB.result.output_size &&
				std::memcmp(runA.result.output_data, runB.result.output_data, runA.result.output_size) == 0,
				modeLabel + ": output differs"
			);
			solAssert(
				logsEqual(runA.logs, runB.logs),
				modeLabel + ": logs differ"
			);
			solAssert(
				storageEqual(runA.storage, runA.contractCreationOrder, runB.storage, runB.contractCreationOrder),
				modeLabel + ": storage differs"
			);
			solAssert(
				transientStorageEqual(runA.transientStorage, runB.transientStorage),
				modeLabel + ": transient storage differs"
			);
		}

		// Compare revert data when both reverted
		if (runA.result.status_code == EVMC_REVERT && runB.result.status_code == EVMC_REVERT)
		{
			solAssert(
				runA.result.output_size == runB.result.output_size &&
				std::memcmp(runA.result.output_data, runB.result.output_data, runA.result.output_size) == 0,
				modeLabel + ": revert data differs"
			);
		}
	}
	catch (evmasm::StackTooDeepException const&)
	{
		// Stack-too-deep in legacy codegen is expected for some inputs.
	}
	catch (langutil::StackTooDeepError const&)
	{
		// Stack-too-deep in IR codegen is expected for some inputs.
	}
}
