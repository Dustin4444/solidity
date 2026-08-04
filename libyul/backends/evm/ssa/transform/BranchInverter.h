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
/**
 * Inverts conditional jumps whose condition is a single-use `iszero`, branching on the negation's
 * operand directly with swapped jump targets.
 */
#pragma once

namespace solidity::yul::ssa
{

class SSACFG;

namespace transform
{
/// Rewrites every ConditionalJump on `iszero(x)` into a ConditionalJump on `x` with the two jump
/// targets swapped, provided the `iszero` is defined in the exiting block and the condition read
/// is its only use. The dead `iszero` is turned into a Nop; `iszero(iszero(x))` chains are peeled
/// one negation at a time.
void invertBranches(SSACFG& _cfg);
}

}
