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
#include <libyul/optimiser/StackToMemoryMover.h>
#include <libyul/optimiser/NameCollector.h>
#include <libyul/backends/evm/EVMDialect.h>

#include <libyul/AST.h>

#include <libsolutil/CommonData.h>

#include <range/v3/algorithm/none_of.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <range/v3/range/conversion.hpp>

using namespace solidity;
using namespace solidity::yul;

void StackToMemoryMover::run(
	OptimiserStepContext&,
	u256,
	std::map<YulName, uint64_t> const&,
	uint64_t,
	Block&
)
{
}

StackToMemoryMover::StackToMemoryMover(
	OptimiserStepContext& _context,
	std::map<YulName, NameWithDebugDataList> _functionReturnVariables
):
m_context(_context),
m_nameDispenser(_context.dispenser),
m_functionReturnVariables(std::move(_functionReturnVariables))
{
	auto const* evmDialect = dynamic_cast<EVMDialect const*>(&_context.dialect);
	yulAssert(
		evmDialect && evmDialect->providesObjectAccess(),
		"StackToMemoryMover can only be run on objects using the EVMDialect with object access."
	);
}
