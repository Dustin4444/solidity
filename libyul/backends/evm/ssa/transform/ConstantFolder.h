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
 * Constant-folds pure builtin calls whose arguments are compile-time constants.
 */
#pragma once

namespace solidity::yul::ssa
{

class SSACFG;

namespace transform
{
/// Replaces every pure builtin call whose argument tree evaluates to compile-time constants by the
/// resulting literal constant (deduplicated via the instruction store). Returns whether anything
/// was folded; trivial-phi elimination behind a fold can enable further folding, so callers should
/// iterate the containing cleanup sequence until this returns false.
bool foldConstants(SSACFG& _cfg);
}

}
