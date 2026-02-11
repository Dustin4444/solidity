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
 * Local common subexpression elimination on the SSA CFG.
 *
 * Deduplicates identical movable builtin calls within each basic block,
 * replacing duplicate outputs with the first occurrence's output.
 */

#pragma once

#include <libyul/backends/evm/ssa/SSACFG.h>

namespace solidity::yul::ssa
{

struct ControlFlow;

class SSACFGLocalCSE
{
public:
	static void run(ControlFlow& _controlFlow);
private:
	static void run(SSACFG& _cfg);
};

}
