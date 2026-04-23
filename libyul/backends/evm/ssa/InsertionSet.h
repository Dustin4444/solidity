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

#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace solidity::yul::ssa
{

/// Buffers instructions to be inserted into a block's instruction vector and
/// applies them in a single O(n + k) pass.
///
/// Usage:
///   InsertionSet set;
///   for (size_t i = 0; i < target.size(); ++i)
///       if (shouldInsertBefore(i))
///           set.insert(i, newInstId);
///   set.execute(target);
///
/// Insert positions are indices into the original vector at the time insert()
/// was called; multiple insertions at the same position are applied in the
/// order they were added.
class InsertionSet
{
public:
	/// Schedules _inst to be inserted before position _position in the target
	/// vector when execute() is called.
	void insert(std::size_t _position, InstId _inst);

	/// Rebuilds _instructions with all scheduled insertions applied. After
	/// return, the InsertionSet is empty.
	void execute(std::vector<InstId>& _instructions);

	bool empty() const { return m_insertions.empty(); }
	std::size_t size() const { return m_insertions.size(); }

private:
	std::vector<std::pair<std::size_t, InstId>> m_insertions;
};

}
