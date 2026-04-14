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

#include <libsolutil/log/LoggerRegistry.h>

#include <boost/test/unit_test.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace solidity::util::test
{

using log::Level;
using log::Logger;
using Presets = std::vector<std::pair<std::string, Level>>;

BOOST_AUTO_TEST_SUITE(LoggerTest, *boost::unit_test::label("nooptions"))

BOOST_AUTO_TEST_CASE(resolveLevel_no_presets_yields_off)
{
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssa", {}) == Level::off);
}

BOOST_AUTO_TEST_CASE(resolveLevel_global_default_applies)
{
	Presets const presets{{"", Level::debug}};
	BOOST_CHECK(solidity::log::resolveLevel("yul", presets) == Level::debug);
	BOOST_CHECK(solidity::log::resolveLevel("anything.else", presets) == Level::debug);
}

BOOST_AUTO_TEST_CASE(resolveLevel_most_specific_prefix_wins)
{
	Presets const presets{
		{"", Level::warn},
		{"yul.ssa", Level::debug},
		{"yul.ssa.codetransform.shuffler", Level::off},
	};
	BOOST_CHECK(solidity::log::resolveLevel("yul", presets) == Level::warn);
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssa.foo", presets) == Level::debug);
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssa.codetransform", presets) == Level::debug);
	BOOST_CHECK(
		solidity::log::resolveLevel("yul.ssa.codetransform.shuffler", presets) == Level::off
	);
}

BOOST_AUTO_TEST_CASE(resolveLevel_dot_boundary_required)
{
	Presets const presets{{"yul.ssa", Level::debug}};
	BOOST_CHECK(solidity::log::resolveLevel("yul.s", presets) == Level::off);
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssax", presets) == Level::off);
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssa", presets) == Level::debug);
	BOOST_CHECK(solidity::log::resolveLevel("yul.ssa.x", presets) == Level::debug);
}

BOOST_AUTO_TEST_CASE(resolveLevel_preset_order_irrelevant)
{
	Presets const a{{"", Level::warn}, {"yul", Level::debug}};
	Presets const b{{"yul", Level::debug}, {"", Level::warn}};
	BOOST_CHECK(solidity::log::resolveLevel("yul.x", a) == Level::debug);
	BOOST_CHECK(solidity::log::resolveLevel("yul.x", b) == Level::debug);
	BOOST_CHECK(solidity::log::resolveLevel("other", a) == Level::warn);
	BOOST_CHECK(solidity::log::resolveLevel("other", b) == Level::warn);
}

BOOST_AUTO_TEST_CASE(parseLevel_roundtrip)
{
	using log::parseLevel;
	BOOST_CHECK(parseLevel("trace") == Level::trace);
	BOOST_CHECK(parseLevel("debug") == Level::debug);
	BOOST_CHECK(parseLevel("warn") == Level::warn);
	BOOST_CHECK(parseLevel("off") == Level::off);

	BOOST_CHECK(!parseLevel("info"));
	BOOST_CHECK(!parseLevel("error"));
	BOOST_CHECK(!parseLevel(""));
	BOOST_CHECK(!parseLevel("DEBUG"));
	BOOST_CHECK(!parseLevel("garbage"));
}

BOOST_AUTO_TEST_CASE(argument_not_evaluated_when_off)
{
	Logger const logger("test", Level::off, std::cerr);
	int counter = 0;
	solDebug(logger, "{}", (counter++, 0));
	BOOST_CHECK_EQUAL(counter, 0);
}

BOOST_AUTO_TEST_CASE(argument_evaluated_when_enabled)
{
	std::ostringstream capture;
	Logger const logger("test", Level::debug, capture);

	int counter = 0;
	solDebug(logger, "{}", (counter++, 42));
	BOOST_CHECK_EQUAL(counter, 1);
	BOOST_CHECK_EQUAL(capture.str(), "42\n");
}

BOOST_AUTO_TEST_CASE(newline_is_appended)
{
	std::ostringstream capture;
	Logger const logger("test", Level::debug, capture);

	solDebug(logger, "hello");
	solDebug(logger, "world");
	BOOST_CHECK_EQUAL(capture.str(), "hello\nworld\n");
}

BOOST_AUTO_TEST_CASE(raw_variant_does_not_append_newline)
{
	std::ostringstream capture;
	Logger const logger("test", Level::debug, capture);

	solDebugRaw(logger, "part1 ");
	solDebugRaw(logger, "part2");
	solDebug(logger, "");
	BOOST_CHECK_EQUAL(capture.str(), "part1 part2\n");
}

BOOST_AUTO_TEST_CASE(setOutput_redirects_subsequent_writes)
{
	std::ostringstream first;
	Logger logger("test", Level::debug, first);
	solDebug(logger, "to-first");
	BOOST_CHECK_EQUAL(first.str(), "to-first\n");

	std::ostringstream second;
	logger.setOutput(second);
	solDebug(logger, "to-second");
	BOOST_CHECK_EQUAL(second.str(), "to-second\n");
}

BOOST_AUTO_TEST_SUITE_END()

}
