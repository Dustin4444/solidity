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

#include <test/libsolidity/util/Common.h>
#include <test/libsolidity/util/StandardJSONOutput.h>

#include <optional>
#include <vector>

namespace solidity::frontend::test
{

using namespace output;

class StandardJSONOutputExt
{
public:
	///
	explicit StandardJSONOutputExt(StandardJSONOutput _base):
		m_base(std::move(_base))
	{}

	///
	bool success() const;

	///
	std::vector<Error> const& errors() const;

	///
	std::vector<Contract const*> const contracts() const;

	///
	Contract const* contract(ContractName const& _contractName = {}) const;

private:
	///
	StandardJSONOutput m_base;
	///
	mutable std::optional<std::vector<Contract const*>> m_contracts;
};

}
