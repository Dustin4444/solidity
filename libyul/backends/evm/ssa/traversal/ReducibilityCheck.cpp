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

#include <libyul/backends/evm/ssa/traversal/ReducibilityCheck.h>

#include <libyul/backends/evm/ssa/traversal/DominatorTree.h>
#include <libyul/backends/evm/ssa/traversal/ForwardTopologicalSort.h>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::traversal;

bool traversal::isReducibleCFG(ForwardTopologicalSort const& _topologicalSort, DominatorTree const& _dominatorTree)
{
	for (auto const src: _topologicalSort.preOrder())
	{
		bool reducible = true;
		_topologicalSort.cfg().block(SSACFG::BlockId{src}).forEachExit([&](SSACFG::BlockId const& _target) {
			if (
				_topologicalSort.backEdge(SSACFG::BlockId{src}, _target) &&
				!_dominatorTree.dominates(_target, SSACFG::BlockId{src})
			)
				reducible = false;
		});
		if (!reducible)
			return false;
	}
	return true;
}
