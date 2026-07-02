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

#include <libyul/backends/evm/ssa/transform/CommonSubexpressionEliminator.h>

#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/traversal/DominatorTree.h>
#include <libyul/backends/evm/ssa/traversal/ForwardTopologicalSort.h>

#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::transform;

namespace
{
// Opcode-agnostic value number: (opcode, payload discriminator, resolved input value ids). Two pure
// operations with equal keys compute the same value, so one can forward to the other.
using Key = std::tuple<InstOpcode, std::uint64_t, std::vector<InstId::ValueType>>;

/// A pure operation is a deterministic function of its inputs with no observable effect, so two pure
/// operations with equal opcode + payload + inputs compute equal outputs. This single predicate decides
/// what may be deduplicated; extending it to further opcodes (e.g. side-effect-free Calls, once
/// interprocedural purity is available) is the intended way to widen CSE beyond builtins.
bool isPure(SSACFG const& _cfg, InstId const _id)
{
	switch (_cfg.kindOf(_id))
	{
	case InstOpcode::BuiltinCall:
	{
		auto const& payload = _cfg.builtinPayload(_id);
		// Literal-argument builtins (e.g. `datasize("A")`) fold their literals into the emitted code
		// rather than the input list, so equal inputs do not imply an equal result.
		if (!payload.literalArguments.empty())
			return false;
		return _cfg.evmDialect.builtin(payload.builtin).sideEffects.movable;
	}
	case InstOpcode::Call:
		// Movability of the callee is determined from its data side-effects at build time.
		return _cfg.callPayload(_id).movable;
	// MemoryGuard/others: not recomputable from inputs.
	default:
		return false;
	}
}

/// Distinguishes operations that share an opcode but denote different computations (which builtin, which
/// callee). Combined with opcode and inputs it uniquely identifies the value a pure operation computes.
std::uint64_t payloadDiscriminator(SSACFG const& _cfg, InstId const _id)
{
	switch (_cfg.kindOf(_id))
	{
	case InstOpcode::BuiltinCall:
		return _cfg.builtinPayload(_id).builtin.id;
	case InstOpcode::Call:
		return _cfg.callPayload(_id).graphID;
	default:
		return 0;
	}
}

Key keyOf(SSACFG const& _cfg, InstId const _id)
{
	SSACFG::Inst const& inst = _cfg.inst(_id);
	std::vector<InstId::ValueType> inputs;
	inputs.reserve(inst.inputs.size());
	for (InstId const input: inst.inputs)
		inputs.push_back(_cfg.resolveIdentity(input).value);
	return {_cfg.kindOf(_id), payloadDiscriminator(_cfg, _id), std::move(inputs)};
}
}

void transform::eliminateCommonSubexpressions(SSACFG& _cfg)
{
	traversal::ForwardTopologicalSort const sort(_cfg);
	traversal::DominatorTree const dom(sort);

	// Dominator-tree children per block; a value defined in a block is visible exactly to the blocks it
	// dominates, i.e. its dominator-tree subtree.
	std::vector<std::vector<SSACFG::BlockId>> children(_cfg.numBlocks());
	for (SSACFG::BlockId const block: _cfg.liveBlocks())
		if (block != _cfg.entry)
			children[dom.immediateDominator(block).value].push_back(block);

	// Available expressions along the current dominator-tree path. Because we only ever insert a key
	// that is absent and erase it when leaving the defining block's subtree, any hit is guaranteed to be
	// dominated by the current instruction, so forwarding to it is legal.
	std::map<Key, InstId> available;

	// Explicit stack DFS over the dominator tree to avoid unbounded recursion on deep graphs.
	struct Frame
	{
		SSACFG::BlockId block;
		std::vector<Key> insertedKeys;
		std::size_t nextChild;
		bool entered;
	};
	std::vector<Frame> stack;
	stack.push_back({_cfg.entry, {}, 0, false});
	while (!stack.empty())
	{
		Frame& frame = stack.back();
		if (!frame.entered)
		{
			frame.entered = true;
			for (InstId const id: _cfg.block(frame.block).instructions)
			{
				if (!isPure(_cfg, id))
					continue;
				Key key = keyOf(_cfg, id);
				if (auto const it = available.find(key); it != available.end())
					_cfg.forwardProducer(id, it->second);
				else
				{
					available.emplace(key, id);
					frame.insertedKeys.push_back(std::move(key));
				}
			}
		}
		if (frame.nextChild < children[frame.block.value].size())
		{
			SSACFG::BlockId const child = children[frame.block.value][frame.nextChild++];
			stack.push_back({child, {}, 0, false});
		}
		else
		{
			for (Key const& key: frame.insertedKeys)
				available.erase(key);
			stack.pop_back();
		}
	}
}
