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

#include <libyul/backends/evm/ssa/transform/OptimizationPipeline.h>

#include <libyul/backends/evm/ssa/transform/BranchInverter.h>
#include <libyul/backends/evm/ssa/transform/ConstantConditionFolder.h>
#include <libyul/backends/evm/ssa/transform/ConstantFolder.h>
#include <libyul/backends/evm/ssa/transform/IdentityAndNopRemover.h>
#include <libyul/backends/evm/ssa/transform/JumpThreader.h>
#include <libyul/backends/evm/ssa/transform/Outliner.h>
#include <libyul/backends/evm/ssa/transform/TrivialPhiEliminator.h>
#include <libyul/backends/evm/ssa/transform/UnreachableBlockCleaner.h>

#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>

using namespace solidity::yul::ssa;

void transform::optimize(ControlFlowGraphs& _cfgs)
{
	for (auto& cfg: _cfgs.functionGraphs)
	{
		// Folding can cascade: removing a dead edge can leave single-entry blocks whose phis
		// become trivial, which in turn can make further values and conditions constant. Each
		// round retires at least one builtin call or conditional exit, so this terminates.
		bool foldedAny = false;
		do
		{
			foldedAny = transform::foldConstants(*cfg);
			foldedAny = transform::foldConstantConditions(*cfg) || foldedAny;
			transform::cleanUnreachableBlocks(*cfg);
			transform::eliminateTrivialPhis(*cfg);
			transform::removeIdentitiesAndNops(*cfg);
		} while (foldedAny);
		{
			transform::invertBranches(*cfg);
			transform::removeIdentitiesAndNops(*cfg);
		}
		{
			transform::threadJumps(*cfg);
			transform::cleanUnreachableBlocks(*cfg);
		}
	}
	// transform::runOutliner(_cfgs);
}
