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
 * Container of (unparsed) Yul functions identified by name which are meant to be generated
 * only once.
 */

#include <libsolidity/codegen/MultiUseYulFunctionCollector.h>

#include <liblangutil/Exceptions.h>
#include <libsolutil/Whiskers.h>
#include <libsolutil/StringUtils.h>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::util;

namespace
{

// Compile-time checks for FunctionCreatorConcept.
static_assert(FunctionCreatorConcept<decltype([](int const*, std::string const&){}), int>);
static_assert(!FunctionCreatorConcept<decltype([x = 0](int const*, std::string const&){ std::ignore = x; }), int>);

}

std::string MultiUseYulFunctionCollector::requestedFunctions()
{
	std::string result = std::move(m_code);
	m_code.clear();
	m_requestedFunctionKeys.clear();
	m_keyToYulName.clear();
	m_baseCounters.clear();
	m_yulNames.clear();
#ifndef NDEBUG
	m_functionKeyDebugInfo.clear();
	m_functionBodies.clear();
#endif
	return result;
}

std::string MultiUseYulFunctionCollector::createFunction(
	std::string const& _name,
	std::function<std::string(std::vector<std::string>&, std::vector<std::string>&)> const& _creator
)
{
	return createFunction(_name, std::function<std::string()>([&]() -> std::string {
		std::vector<std::string> arguments;
		std::vector<std::string> returnParameters;
		std::string body = _creator(arguments, returnParameters);
		solAssert(!body.empty(), "");
		return Whiskers(R"(
			function <functionName>(<args>)<?+retParams> -> <retParams></+retParams> {
				<body>
			}
		)")
		("functionName", _name)
		("args", joinHumanReadable(arguments))
		("retParams", joinHumanReadable(returnParameters))
		("body", body)
		.render();
	}));
}

std::string MultiUseYulFunctionCollector::createFunction(
	std::string const& _name,
	std::function<std::string()> const& _creator,
	bool _pureCreator
)
{
	solAssert(!_name.empty(), "");
	FunctionKey const key{_name};
	if (!m_requestedFunctionKeys.contains(key))
	{
		m_requestedFunctionKeys.insert(key);
		solAssert(
			m_yulNames.insert(_name).second,
			"Yul function name collides with existing name: " + _name
		);
		m_keyToYulName[key] = _name;
		std::string function = _creator();
		solAssert(!function.empty(), "");
		solAssert(function.find("function " + _name + "(") != std::string::npos, "Function not properly named.");
		m_code += function;
#ifndef NDEBUG
		if (_pureCreator)
			m_functionBodies[_name] = function;
#endif
	}
#ifndef NDEBUG
	else if (_pureCreator)
	{
		// Re-run the creator and verify body stability.
		// Catches incomplete cache keys where the creator depends on state not reflected in the name.
		std::string regenerated = _creator();
		solAssert(
			m_functionBodies.at(_name) == regenerated,
			"Function body mismatch on cache hit for: " + _name
		);
	}
	else
	{
		// Impure creator (e.g. IRGenerator) — deduplication should never happen.
		solAssert(false, "Unexpected cache hit for impure creator: " + _name);
	}
#endif
	return _name;
}
