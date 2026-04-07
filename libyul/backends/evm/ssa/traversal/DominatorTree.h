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

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <vector>

namespace solidity::yul::ssa::traversal
{

class ForwardTopologicalSort;

/// Computes the dominator tree of an SSACFG using the Semi-NCA algorithm
class DominatorTree
{
public:
	explicit DominatorTree(ForwardTopologicalSort const& _sort);

	/// Returns true if `_dominator` dominates `_dominated`. Runs in constant time.
	bool dominates(SSACFG::BlockId _dominator, SSACFG::BlockId _dominated) const;

	/// Returns the immediate dominator of a reachable block.
	/// For the entry block, returns the entry block itself.
	SSACFG::BlockId immediateDominator(SSACFG::BlockId _block) const;

private:
	static void compressPath(
		std::vector<std::size_t>& _ancestor,
		std::vector<std::size_t>& _label,
		std::vector<std::size_t> const& _semi,
		std::size_t _v
	);

	/// indexed by block ID value, stores the block ID value of the immediate dominator
	std::vector<SSACFG::BlockId::ValueType> m_idom;
	/// DFS numbering on the dominator tree
	std::vector<std::size_t> m_domTreePreOrder;
	std::vector<std::size_t> m_domTreeMaxSubtreePreOrder;
};

}
