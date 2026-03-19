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

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

using namespace solidity::test::fuzzer;
using namespace solidity::test::sol2protofuzzer;
using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::test;
using namespace solidity::util;

/// Helper: compile, deploy, and execute a test contract.
/// Returns the evmc::Result, or a result with EVMC_INTERNAL_ERROR on failure.
/// @param _extraCalldataHex hex-encoded bytes appended after the function
/// selector, accessible via calldataload in the generated Solidity code.
static evmc::Result runOnce(
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
		methodName
	);
	return evmoneUtil.compileDeployAndExecute({}, _extraCalldataHex);
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

	langutil::EVMVersion version;
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
		auto resultNoOpt = runOnce(version, source, OptimiserSettings::minimal(), viaIR, extraCalldataHex);

		// Run 2: with optimization
		auto resultOpt = runOnce(version, source, OptimiserSettings::standard(), viaIR, extraCalldataHex);

		// Differential check: if both succeed, outputs must match.
		// TODO: Consider also asserting when exactly one run succeeds and the
		// other reverts — that is also a potential miscompilation bug. Currently
		// we only check the both-succeed case to avoid false positives from
		// legitimate differences (e.g. gas-related reverts).
		if (resultNoOpt.status_code == EVMC_SUCCESS && resultOpt.status_code == EVMC_SUCCESS)
		{
			solAssert(
				resultNoOpt.output_size == resultOpt.output_size &&
				std::memcmp(resultNoOpt.output_data, resultOpt.output_data, resultNoOpt.output_size) == 0,
				"Sol proto2 fuzzer: optimized vs non-optimized output differs"
			);
		}
	}
	catch (evmasm::StackTooDeepException const&)
	{
		// Stack-too-deep in legacy codegen is expected for some inputs.
	}
}
