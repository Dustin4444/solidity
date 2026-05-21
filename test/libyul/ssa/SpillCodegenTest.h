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

#pragma once

#include <test/TestCase.h>

#include <memory>

namespace solidity::yul::test::ssa
{

/// Drives the SSA-CFG backend end to end (`SSACFGBuilder` -> optimizer -> `StackLayoutGenerator`
/// -> `SpillSet::feasilize` -> `MemoryAddressing` -> `CodeTransform`) on a Yul object and prints,
/// per CFG, what was spilled (value -> memory address) and the MSTORE schedule (which value is
/// written at which instruction), followed by the resulting EVM assembly text. Lets us pin the
/// spilling behavior of a given Yul snippet — including the def-site cascade for phis that are
/// unreachable at their definition — against a checked-in expectation.
class SpillCodegenTest: public frontend::test::TestCase
{
public:
	static std::unique_ptr<TestCase> create(Config const& _config);
	explicit SpillCodegenTest(std::string const& _filename);
	TestResult run(std::ostream& _stream, std::string const& _linePrefix, bool _formatted) override;
};

}
