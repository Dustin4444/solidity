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

#include <libyul/backends/evm/ssa/StackShuffler.h>

namespace solidity::yul::ssa
{

namespace detail2
{
struct PushOp
{
	StackSlot slot;
};
struct PopOp {};
struct DupOp
{
	StackDepth depth;
};
struct SwapOp
{
	StackDepth depth;
};
using StackOp = std::variant<PushOp, PopOp, DupOp, SwapOp>;

struct Result
{
	StackShufflerResult result;
	std::vector<StackOp> ops;
};

struct Target
{
	StackData const& args;
	StackSlotLiveness const& liveOut;
	std::size_t targetStackSize;
};
static size_t constexpr reachableStackDepth = 16;
// TODO: Add spill set
Result shuffle(StackData const & source, Target target);
}


[[nodiscard]] StackShufflerResult martinShuffle(
		StackData& _stack,
		StackData const& _args,
		StackSlotLiveness const& _liveOut,
		std::size_t _targetStackSize,
		spill::SpillSet const* _spilledVariables = nullptr
	);
}
