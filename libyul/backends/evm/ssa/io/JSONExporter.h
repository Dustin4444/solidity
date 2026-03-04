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

#include <libyul/backends/evm/ssa/ControlFlow.h>
#include <libyul/backends/evm/ssa/StackLayout.h>
#include <libsolutil/JSON.h>

#include <vector>

namespace solidity::yul::ssa::io::json
{

/// Exports the control flow graph to JSON.
/// @param _liveness Optional liveness analysis results; if non-null, liveness info is included per block.
/// @param _stackLayouts Optional stack layouts (one per function graph, in the same order as
///                      ControlFlow::functionGraphs); if non-null, stack layout info is included per block.
Json exportControlFlow(
	ControlFlow const& _controlFlow,
	ControlFlowLiveness const* _liveness,
	std::vector<SSACFGStackLayout> const* _stackLayouts = nullptr
);

}
