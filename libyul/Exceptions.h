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
 * Exceptions in Yul.
 */

#pragma once

#include <libsolutil/Exceptions.h>
#include <libsolutil/Assertions.h>

#include <libyul/YulName.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/facilities/overload.hpp>

namespace solidity::yul
{

struct YulException: virtual util::Exception {};
struct OptimizerException: virtual YulException {};
struct CodegenException: virtual YulException {};
struct YulAssertion: virtual util::Exception {};

struct StackTooDeepError: virtual YulException
{
	StackTooDeepError(YulName _variable, int _depth, std::string const& _message):
		variable(_variable), depth(_depth)
	{
		*this << util::errinfo_comment(_message);
	}
	StackTooDeepError(YulName _functionName, YulName _variable, int _depth, std::string const& _message):
		functionName(_functionName), variable(_variable), depth(_depth)
	{
		*this << util::errinfo_comment(_message);
	}
	YulName functionName;
	YulName variable;
	int depth;
};

/// Assertion that throws an YulAssertion containing the given description if it is not met.
#if !BOOST_PP_VARIADICS_MSVC
#define yulAssert(...) BOOST_PP_OVERLOAD(yulAssert_,__VA_ARGS__)(__VA_ARGS__)
#else
#define yulAssert(...) BOOST_PP_CAT(BOOST_PP_OVERLOAD(yulAssert_,__VA_ARGS__)(__VA_ARGS__),BOOST_PP_EMPTY())
#endif

#define yulAssert_1(CONDITION) \
	yulAssert_2((CONDITION), "")

#define yulAssert_2(CONDITION, DESCRIPTION) \
	assertThrowWithDefaultDescription( \
		(CONDITION), \
		::solidity::yul::YulAssertion, \
		(DESCRIPTION), \
		"Yul assertion failed" \
	)

/// Throws an exception with a given description and extra information about the location the
/// exception was thrown from.
/// @param _exceptionType The type of the exception to throw (not an instance).
/// @param _description The message that describes the error.
#define yulThrow(_exceptionType, _description) \
	::boost::throw_exception( \
		_exceptionType() << \
		::solidity::util::errinfo_comment((_description)) << \
		::boost::throw_function(ETH_FUNC) << \
		::boost::throw_file(__FILE__) << \
		::boost::throw_line(__LINE__) \
	)

/// Throws an exception if condition is not met with a given description and extra information about the location the
/// exception was thrown from.
/// @param _condition if condition is not met, specified exception will be thrown.
/// @param _exceptionType The type of the exception to throw (not an instance).
/// @param _description The message that describes the error.
#define yulRequire(_condition, _exceptionType, _description) \
	if (!(_condition)) \
		yulThrow(_exceptionType, (_description))

}
