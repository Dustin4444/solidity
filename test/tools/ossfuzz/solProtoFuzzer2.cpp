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

#include <evmone/evmone.h>
#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>

static evmc::VM evmone = evmc::VM{evmc_create_evmone()};

using namespace solidity::test::fuzzer;
using namespace solidity::test::sol2protofuzzer;
using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::test;
using namespace solidity::util;

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

	// Use the default (latest) EVM version
	langutil::EVMVersion version;
	EVMHost hostContext(version, evmone);

	// Choose optimizer settings based on proto flags
	auto optimiserSettings = _input.optimize()
		? OptimiserSettings::standard()
		: OptimiserSettings::minimal();

	std::string contractName = "C";
	std::string methodName = "test()";
	StringMap source({{"test.sol", sol_source}});
	CompilerInput cInput(
		version,
		source,
		contractName,
		optimiserSettings,
		{},
		/*debugFailure=*/false,
		/*viaIR=*/_input.via_ir()
	);
	EvmoneUtility evmoneUtil(
		hostContext,
		cInput,
		contractName,
		/*libraryName=*/"",
		methodName
	);
	auto result = evmoneUtil.compileDeployAndExecute();
	// If compilation and execution succeeded, the test contract should
	// return 0. If it doesn't, we found a codegen or optimizer bug.
	if (result.status_code == EVMC_SUCCESS)
		solAssert(
			EvmoneUtility::zeroWord(result.output_data, result.output_size),
			"Sol proto2 fuzzer: Output incorrect"
		);
}
