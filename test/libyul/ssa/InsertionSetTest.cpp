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
/**
 * Unit tests for InsertionSet.
 */

#include <libyul/backends/evm/ssa/InsertionSet.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <boost/test/unit_test.hpp>

#include <vector>

namespace solidity::yul::ssa::test
{

namespace
{
InstId id(std::uint32_t _v) { return InstId{_v}; }

std::vector<std::uint32_t> values(std::vector<InstId> const& _ids)
{
	std::vector<std::uint32_t> out;
	out.reserve(_ids.size());
	for (auto const& e: _ids)
		out.push_back(e.value);
	return out;
}
}

BOOST_AUTO_TEST_SUITE(InsertionSetTest)

BOOST_AUTO_TEST_CASE(empty_set_is_noop)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2), id(3)};
	set.execute(target);
	std::vector<std::uint32_t> const expected{1, 2, 3};
	BOOST_CHECK(values(target) == expected);
	BOOST_CHECK(set.empty());
}

BOOST_AUTO_TEST_CASE(insert_at_front)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2), id(3)};
	set.insert(0, id(100));
	set.execute(target);
	std::vector<std::uint32_t> const expected{100, 1, 2, 3};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(insert_at_end)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2), id(3)};
	set.insert(3, id(100));
	set.execute(target);
	std::vector<std::uint32_t> const expected{1, 2, 3, 100};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(insert_in_middle)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2), id(3)};
	set.insert(1, id(100));
	set.execute(target);
	std::vector<std::uint32_t> const expected{1, 100, 2, 3};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(multiple_inserts_same_position_preserve_order)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2)};
	set.insert(1, id(100));
	set.insert(1, id(101));
	set.insert(1, id(102));
	set.execute(target);
	std::vector<std::uint32_t> const expected{1, 100, 101, 102, 2};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(insert_into_empty_target)
{
	InsertionSet set;
	std::vector<InstId> target;
	set.insert(0, id(100));
	set.insert(0, id(101));
	set.execute(target);
	std::vector<std::uint32_t> const expected{100, 101};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(many_insertions_interleaved)
{
	InsertionSet set;
	std::vector<InstId> target{id(1), id(2), id(3), id(4)};
	set.insert(0, id(10));
	set.insert(2, id(20));
	set.insert(2, id(21));
	set.insert(4, id(40));
	set.execute(target);
	std::vector<std::uint32_t> const expected{10, 1, 2, 20, 21, 3, 4, 40};
	BOOST_CHECK(values(target) == expected);
}

BOOST_AUTO_TEST_CASE(execute_clears_pending)
{
	InsertionSet set;
	std::vector<InstId> target{id(1)};
	set.insert(0, id(100));
	set.execute(target);
	BOOST_CHECK(set.empty());

	std::vector<InstId> target2{id(2)};
	set.execute(target2);
	std::vector<std::uint32_t> const expected{2};
	BOOST_CHECK(values(target2) == expected);
}

BOOST_AUTO_TEST_SUITE_END()

}
