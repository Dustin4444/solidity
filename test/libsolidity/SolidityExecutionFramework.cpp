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
 * @author Christian <c@ethdev.com>
 * @date 2016
 * Framework for executing Solidity contracts and testing them against C++ implementation.
 */

#include <functional>
#include <test/libsolidity/SolidityExecutionFramework.h>
#include <test/libsolidity/util/Common.h>
#include <test/libsolidity/util/Compiler.h>
#include <test/libsolidity/util/SoltestErrors.h>

#include <liblangutil/DebugInfoSelection.h>
#include <libyul/Exceptions.h>
#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <boost/test/framework.hpp>

#include <range/v3/algorithm.hpp>

#include <cstdlib>
#include <iostream>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity::langutil;
using namespace solidity::test;

bytes SolidityExecutionFramework::multiSourceCompileContract(
	std::map<std::string, std::string> const& _sourceCode,
	std::string const& _contractName,
	std::map<std::string, Address> const& _libraryAddresses,
	std::optional<std::string> const& _mainSourceName
)
{
	if (_mainSourceName.has_value())
		solAssert(_sourceCode.find(_mainSourceName.value()) != _sourceCode.end(), "");

	m_compilerInput = CompilerInput{
		.sourceCode = withPreamble(
			_sourceCode,
			solidity::test::CommonOptions::get().useABIEncoderV1 // _addAbicoderV1Pragma
		),
		.libraryAddresses = _libraryAddresses,
		.evmVersion = m_evmVersion,
		.eofVersion = m_eofVersion,
		.viaIR = m_compileViaYul,
		.optimiserSettings = m_optimiserSettings,
		.metadataAppendCBOR = m_appendCBORMetadata,
		.metadataHash = m_metadataHash,
		.revertStrings = m_revertStrings,
	};

	CompilerOutput const& output = m_compiler.compile(m_compilerInput);
	if (!output.success())
	{
		// The testing framework expects an exception for
		// "unimplemented" yul IR generation.
		auto codeGenError = ranges::find_if(
			output.errors(),
			[](auto const& _e) { return _e->type() == Error::Type::CodeGenerationError; }
		);

		if (m_compileViaYul && codeGenError != output.errors().end())
			BOOST_THROW_EXCEPTION(*(*codeGenError));

		m_compiler.printErrors();
		BOOST_ERROR("Compiling contract failed");
	}

	// Construct `ContractName` with the contract name given, and use `_mainSourceName`
	// if the contract's name source prefix is empty.
	auto const [sourceName, contractName, _] = decomposeContractName(_contractName);
	ContractName lookupName{
		sourceName.empty() ? _mainSourceName.value_or("") : sourceName,
		contractName
	};

	auto const* contract = output.contract(lookupName);
	soltestAssert(contract);
	soltestAssert(!contract->hasUnlinkedReferences);

	if (m_showMetadata)
		std::cout << "metadata: " << contract->metadata << std::endl;

	return contract->object;
}

bytes SolidityExecutionFramework::compileContract(
	std::string const& _sourceCode,
	std::string const& _contractName,
	std::map<std::string, Address> const& _libraryAddresses
)
{
	return multiSourceCompileContract(
		{{"", _sourceCode}},
		_contractName,
		_libraryAddresses,
		std::nullopt
	);
}
