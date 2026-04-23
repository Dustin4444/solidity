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

#include <libyul/backends/evm/ssa/InsertionSet.h>

#include <libyul/Exceptions.h>

#include <algorithm>

using namespace solidity::yul::ssa;

void InsertionSet::insert(std::size_t _position, InstId _inst)
{
	m_insertions.emplace_back(_position, _inst);
}

void InsertionSet::execute(std::vector<InstId>& _instructions)
{
	if (m_insertions.empty())
		return;

	// Stable-sort by target position so that ties preserve insertion order.
	std::stable_sort(
		m_insertions.begin(),
		m_insertions.end(),
		[](auto const& _lhs, auto const& _rhs) { return _lhs.first < _rhs.first; }
	);
	yulAssert(m_insertions.back().first <= _instructions.size(), "Insertion position out of range");

	std::vector<InstId> rebuilt;
	rebuilt.reserve(_instructions.size() + m_insertions.size());
	auto insertionIt = m_insertions.begin();
	for (std::size_t i = 0; i < _instructions.size(); ++i)
	{
		while (insertionIt != m_insertions.end() && insertionIt->first == i)
		{
			rebuilt.push_back(insertionIt->second);
			++insertionIt;
		}
		rebuilt.push_back(_instructions[i]);
	}
	while (insertionIt != m_insertions.end())
	{
		rebuilt.push_back(insertionIt->second);
		++insertionIt;
	}

	_instructions = std::move(rebuilt);
	m_insertions.clear();
}
