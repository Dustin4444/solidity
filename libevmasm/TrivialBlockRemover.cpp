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

#include <libevmasm/TrivialBlockRemover.h>

#include <libevmasm/AssemblyItem.h>
#include <libevmasm/BlockDeduplicator.h>
#include <libevmasm/SemanticInformation.h>

#include <range/v3/view/drop.hpp>
#include <range/v3/view/enumerate.hpp>

bool solidity::evmasm::TrivialBlockRemover::optimise(std::set<size_t> const& _tagsReferencedFromOutside)
{
	std::map<u256, u256> replacedTags;
	for (auto&& [index, item]: m_items | ranges::views::enumerate | ranges::views::drop(1))
	{
		if (item.type() != Tag)
			continue;
		if (index >= m_items.size() - 2)
			continue;
		auto const & next = m_items[index + 1];
		if (next.type() != PushTag)
			continue;
		if (next.data() == item.data())
			continue;
		auto const & nextNext = m_items[index + 2];
		if (nextNext.type() != Operation)
			continue;
		if (nextNext.instruction() != Instruction::JUMP)
			continue;
		auto const & previous = m_items[index - 1];
		if (previous.type() != Operation)
			continue;
		if (previous.instruction() != Instruction::JUMP && !SemanticInformation::terminatesControlFlow(previous))
			continue;
		auto const [subId, tag] = item.splitForeignPushTag();
		solAssert(subId.empty(), "Sub-assembly tag used as label.");
		if (_tagsReferencedFromOutside.contains(tag))
			continue;
		replacedTags.insert({item.data(), next.data()});
	}
	if (!replacedTags.empty())
	{
		return BlockDeduplicator::applyTagReplacement(m_items, replacedTags);
	}
	return false;
}
