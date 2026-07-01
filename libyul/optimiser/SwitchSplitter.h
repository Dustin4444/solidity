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

#include <libyul/optimiser/ASTWalker.h>
#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/Dialect.h>

#include <liblangutil/DebugData.h>

#include <optional>
#include <span>
#include <vector>

namespace solidity::yul
{

/**
 * Transforms a large Yul switch statement into a binary search tree of nested switches,
 * reducing runtime dispatch from O(n) to O(log n).
 *
 * The transformation is applied only when the runtime gas savings over
 * expectedExecutionsPerDeployment executions outweigh the extra code size (creation gas):
 *   runs * 6 * (n - 4) > (17 + k) * createDataGas
 * where k is the default body statement count (proxy for bytecode size).
 * The 17-byte split overhead comes from the gt-switch structure; k accounts for each split
 * duplicating the default body into one additional leaf.
 *
 * Applied recursively: each sub-slice re-evaluates the formula.
 * The default case body is duplicated into each leaf branch when present.
 *
 * Prerequisite: Disambiguator, ExpressionSplitter.
 * The switch expression must already be a simple identifier (ensured by ExpressionSplitter).
 *
 * Important: Can only be used on EVM code.
 */
class SwitchSplitter: public ASTModifier
{
public:
	static constexpr char const* name{"SwitchSplitter"};
	static void run(OptimiserStepContext& _context, Block& _ast);

	using ASTModifier::operator();
	void operator()(Block& _block) override;

private:
	explicit SwitchSplitter(OptimiserStepContext const& _context);

	void simplify(std::vector<Statement>& _statements);

	// Returns replacement statements if the switch should be transformed, nullopt otherwise.
	std::optional<std::vector<Statement>> tryTransform(Switch& _switch);

	// Recursively builds the binary search tree for a sorted, non-empty slice of literal cases.
	std::vector<Statement> buildTree(
		std::span<Case const* const> _cases,
		Identifier const& _expr,
		Block const& _defaultBody,
		langutil::DebugData::ConstPtr _debugData
	);

	bool shouldSplit(size_t _n, size_t _defaultBodySize) const;

	std::optional<BuiltinHandle> m_gtHandle;
	size_t m_runs = 0;
};

}
