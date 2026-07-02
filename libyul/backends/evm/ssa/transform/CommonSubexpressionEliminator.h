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
 * Deduplicates pure (side-effect-free) operations that compute the same value from the same inputs,
 * replacing later occurrences with an Identity to the dominating definition. Candidacy is decided by a
 * single opcode-agnostic purity predicate covering movable builtins and movable user Calls; multi-return
 * producers are handled by forwarding each output to the dominating producer's matching output.
 */
#pragma once

namespace solidity::yul::ssa
{

class SSACFG;

namespace transform
{
/// Deduplicates pure operations across the dominator tree. Leaves Identity forwards behind;
/// run removeIdentitiesAndNops afterwards to clean them up.
void eliminateCommonSubexpressions(SSACFG& _cfg);
}

}
