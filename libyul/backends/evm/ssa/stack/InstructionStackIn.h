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

#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libyul/backends/evm/ssa/SSACFGTypes.h>
#include <libyul/backends/evm/ssa/StackSlotLiveness.h>

namespace solidity::yul::ssa::stack
{

/// Builds the target stack layout for an instruction: a tail derived from `_stack` followed by `_args`.
/// An arg that is on the stack, sees its last use among the args and is dead or spilled after the instruction
/// consumes its tail copy; every other arg leaves the tail unchanged.
/// Beyond that, tail slots are only dropped to bring an arg's copy within DUP/SWAP reach.
/// Only junk, dead values, and spilled values are ever dropped.
StackData buildInstructionStackIn(
	StackData const& _stack,
	StackData const& _args,
	StackSlotLiveness const& _liveOut,
	spill::SpillSet const& _spillSet
);

}
