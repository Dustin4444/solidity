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

namespace solidity::yul::ssa
{
class SSACFG;
namespace transform
{

/// Resolves Identity chains left behind by transformation passes:
///   1. Path-compress each Identity chain to its terminal replacement.
///   2. Rewrite every ValueId operand (instruction inputs, terminator fields
///      in block.exit, upsilon phi payloads) to point at the terminal
///      replacement.
///   3. Remove Identity Insts from every block.instructions vector.
///
/// The Identity Insts themselves remain in the global m_insts pool (no
/// garbage collection yet). See uniformity_of_ssa_cfg_v2.md.
void cleanup(SSACFG& _cfg);

}
}
