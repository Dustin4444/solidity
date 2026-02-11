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

#include <libyul/backends/evm/ssa/SSACFGLocalCSE.h>

#include <libyul/backends/evm/ssa/ControlFlow.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/traversal/ForwardTopologicalSort.h>
#include <libyul/backends/evm/EVMBuiltins.h>
#include <libyul/Dialect.h>

#include <libevmasm/Instruction.h>
#include <libsolutil/Numeric.h>

#include <map>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::evmasm;

namespace
{

struct CSEKey
{
	BuiltinFunction const* builtin;
	std::vector<SSACFG::ValueId> inputs;

	bool operator<(CSEKey const& _other) const
	{
		if (builtin != _other.builtin)
			return builtin < _other.builtin;
		return inputs < _other.inputs;
	}
};

void applySubstitution(
	std::map<SSACFG::ValueId, SSACFG::ValueId> const& _substitutions,
	SSACFG::ValueId& _id
)
{
	if (auto it = _substitutions.find(_id); it != _substitutions.end())
		_id = it->second;
}

/// Evaluate a pure EVM instruction with constant arguments.
/// Returns nullopt for instructions we don't handle.
/// Arguments are in SSA CFG input order (reversed EVM stack order).
std::optional<u256> evaluateInstruction(Instruction _instruction, std::vector<u256> const& _args)
{
	// SSA CFG stores inputs in reverse EVM stack order.
	// EVM: top-of-stack is arg[0]. SSA CFG: inputs[last] is top-of-stack.
	// We reverse to get EVM arg order.
	auto const n = _args.size();
	auto arg = [&](size_t i) -> u256 const& { return _args[n - 1 - i]; };

	switch (_instruction)
	{
	case Instruction::ADD:
		return u256(arg(0) + arg(1));
	case Instruction::MUL:
		return u256(arg(0) * arg(1));
	case Instruction::SUB:
		return u256(arg(0) - arg(1));
	case Instruction::DIV:
		return arg(1) == 0 ? u256(0) : u256(arg(0) / arg(1));
	case Instruction::SDIV:
		return arg(1) == 0 ? u256(0) : s2u(u2s(arg(0)) / u2s(arg(1)));
	case Instruction::MOD:
		return arg(1) == 0 ? u256(0) : u256(arg(0) % arg(1));
	case Instruction::SMOD:
		return arg(1) == 0 ? u256(0) : s2u(u2s(arg(0)) % u2s(arg(1)));
	case Instruction::EXP:
		return exp256(arg(0), arg(1));
	case Instruction::NOT:
		return u256(~arg(0));
	case Instruction::LT:
		return arg(0) < arg(1) ? u256(1) : u256(0);
	case Instruction::GT:
		return arg(0) > arg(1) ? u256(1) : u256(0);
	case Instruction::SLT:
		return u2s(arg(0)) < u2s(arg(1)) ? u256(1) : u256(0);
	case Instruction::SGT:
		return u2s(arg(0)) > u2s(arg(1)) ? u256(1) : u256(0);
	case Instruction::EQ:
		return arg(0) == arg(1) ? u256(1) : u256(0);
	case Instruction::ISZERO:
		return arg(0) == 0 ? u256(1) : u256(0);
	case Instruction::AND:
		return u256(arg(0) & arg(1));
	case Instruction::OR:
		return u256(arg(0) | arg(1));
	case Instruction::XOR:
		return u256(arg(0) ^ arg(1));
	case Instruction::BYTE:
		return arg(0) >= 32 ? u256(0) : u256((arg(1) >> static_cast<unsigned>(8 * (31 - arg(0)))) & 0xff);
	case Instruction::SHL:
		return arg(0) > 255 ? u256(0) : u256(arg(1) << static_cast<unsigned>(arg(0)));
	case Instruction::SHR:
		return arg(0) > 255 ? u256(0) : u256(arg(1) >> static_cast<unsigned>(arg(0)));
	case Instruction::SAR:
	{
		static u256 const hibit = u256(1) << 255;
		if (arg(0) >= 256)
			return (arg(1) & hibit) ? u256(-1) : u256(0);
		unsigned amount = static_cast<unsigned>(arg(0));
		u256 v = arg(1) >> amount;
		if (arg(1) & hibit)
			v |= u256(-1) << (256 - amount);
		return v;
	}
	case Instruction::ADDMOD:
		return arg(2) == 0 ? u256(0) : u256((u512(arg(0)) + u512(arg(1))) % arg(2));
	case Instruction::MULMOD:
		return arg(2) == 0 ? u256(0) : u256((u512(arg(0)) * u512(arg(1))) % arg(2));
	case Instruction::SIGNEXTEND:
		if (arg(0) >= 31)
			return arg(1);
		else
		{
			unsigned testBit = static_cast<unsigned>(arg(0)) * 8 + 7;
			u256 mask = (u256(1) << testBit) - 1;
			return boost::multiprecision::bit_test(arg(1), testBit)
				? u256(arg(1) | ~mask)
				: u256(arg(1) & mask);
		}
	default:
		return std::nullopt;
	}
}

/// Compute predecessors for each block.
std::vector<std::vector<SSACFG::BlockId::ValueType>> computePredecessors(SSACFG const& _cfg)
{
	std::vector<std::vector<SSACFG::BlockId::ValueType>> preds(_cfg.numBlocks());
	for (size_t blockIdx = 0; blockIdx < _cfg.numBlocks(); ++blockIdx)
	{
		SSACFG::BasicBlock const& block = _cfg.block(SSACFG::BlockId{static_cast<SSACFG::BlockId::ValueType>(blockIdx)});
		block.forEachExit([&](SSACFG::BlockId const& _succ) {
			preds[_succ.value].push_back(static_cast<SSACFG::BlockId::ValueType>(blockIdx));
		});
	}
	return preds;
}

/// Compute immediate dominators using the iterative algorithm.
/// Returns idom[block] = immediate dominator of block. idom[entry] = entry.
std::vector<SSACFG::BlockId::ValueType> computeImmediateDominators(
	SSACFG const& _cfg,
	std::vector<SSACFG::BlockId::ValueType> const& _rpo,
	std::vector<std::vector<SSACFG::BlockId::ValueType>> const& _preds
)
{
	// Map from block ID to reverse post-order index
	std::vector<size_t> rpoIndex(_cfg.numBlocks(), SIZE_MAX);
	for (size_t i = 0; i < _rpo.size(); ++i)
		rpoIndex[_rpo[i]] = i;

	// Initialize idoms: undefined except entry
	std::vector<SSACFG::BlockId::ValueType> idom(_cfg.numBlocks(), std::numeric_limits<SSACFG::BlockId::ValueType>::max());
	auto const entry = _cfg.entry.value;
	idom[entry] = entry;

	// Intersect function: find common dominator
	auto intersect = [&](SSACFG::BlockId::ValueType b1, SSACFG::BlockId::ValueType b2) {
		while (b1 != b2)
		{
			while (rpoIndex[b1] > rpoIndex[b2])
				b1 = idom[b1];
			while (rpoIndex[b2] > rpoIndex[b1])
				b2 = idom[b2];
		}
		return b1;
	};

	// Iterate until fixpoint
	bool changed = true;
	while (changed)
	{
		changed = false;
		for (auto b: _rpo)
		{
			if (b == entry)
				continue;

			// Find first processed predecessor
			SSACFG::BlockId::ValueType newIdom = std::numeric_limits<SSACFG::BlockId::ValueType>::max();
			for (auto p: _preds[b])
			{
				if (idom[p] != std::numeric_limits<SSACFG::BlockId::ValueType>::max())
				{
					if (newIdom == std::numeric_limits<SSACFG::BlockId::ValueType>::max())
						newIdom = p;
					else
						newIdom = intersect(newIdom, p);
				}
			}

			if (newIdom != idom[b])
			{
				idom[b] = newIdom;
				changed = true;
			}
		}
	}

	return idom;
}

/// Check if block A dominates block B.
bool dominates(
	SSACFG::BlockId::ValueType _a,
	SSACFG::BlockId::ValueType _b,
	std::vector<SSACFG::BlockId::ValueType> const& _idom
)
{
	// Walk up the dominator tree from B
	while (_b != _a && _idom[_b] != _b)
		_b = _idom[_b];
	return _b == _a;
}

}

void SSACFGLocalCSE::run(ControlFlow& _controlFlow)
{
	for (auto& graph: _controlFlow.functionGraphs)
		run(*graph);
}

void SSACFGLocalCSE::run(SSACFG& _cfg)
{
	std::map<SSACFG::ValueId, SSACFG::ValueId> substitutions;

	// Compute dominance information for global CSE.
	traversal::ForwardTopologicalSort topoSort(_cfg);
	auto const& postOrder = topoSort.postOrder();
	// Reverse post-order for dominance computation
	std::vector<SSACFG::BlockId::ValueType> rpo(postOrder.rbegin(), postOrder.rend());
	auto preds = computePredecessors(_cfg);
	auto idom = computeImmediateDominators(_cfg, rpo, preds);

	// Track which block each Variable is defined in.
	std::map<SSACFG::ValueId, SSACFG::BlockId::ValueType> defBlock;

	// Global CSE map: maps expression key -> (defining Variable, defining block).
	std::map<CSEKey, std::pair<SSACFG::ValueId, SSACFG::BlockId::ValueType>> globalCSE;

	// Phase 1: Constant folding and global CSE in reverse post-order.
	// Iterate to fixpoint because constant folding can enable further folding.
	bool changed = true;
	while (changed)
	{
		changed = false;
		// Clear global CSE map each iteration since definitions may have changed.
		globalCSE.clear();

		for (auto blockIdx: rpo)
		{
			SSACFG::BasicBlock& block = _cfg.block(SSACFG::BlockId{blockIdx});

			for (auto& operation: block.operations)
			{
				// Skip already-substituted operations.
				if (operation.outputs.size() == 1 && substitutions.contains(operation.outputs[0]))
					continue;

				// Apply existing substitutions to inputs.
				for (auto& input: operation.inputs)
					applySubstitution(substitutions, input);

				auto const* builtinCall = std::get_if<SSACFG::BuiltinCall>(&operation.kind);
				if (!builtinCall)
					continue;

				auto const& builtin = builtinCall->builtin.get();

				// Only process movable operations (pure, no side effects).
				if (!builtin.sideEffects.movable)
					continue;

				// Only single-output builtins.
				if (operation.outputs.size() != 1)
					continue;

				// Skip builtins with literal arguments embedded in the AST call.
				if (!builtin.literalArguments.empty())
					continue;

				// Constant folding: if all inputs are literals and we have an EVM instruction,
				// evaluate at compile time.
				auto const& evmBuiltin = static_cast<BuiltinFunctionForEVM const&>(builtin);
				if (evmBuiltin.instruction.has_value())
				{
					bool allLiteral = std::all_of(
						operation.inputs.begin(), operation.inputs.end(),
						[](SSACFG::ValueId const& _id) { return _id.isLiteral(); }
					);
					if (allLiteral)
					{
						std::vector<u256> args;
						args.reserve(operation.inputs.size());
						for (auto const& input: operation.inputs)
							args.push_back(_cfg.literalInfo(input).value);

						if (auto result = evaluateInstruction(*evmBuiltin.instruction, args))
						{
							// Convert operation to a LiteralAssignment to preserve the Variable output.
							SSACFG::ValueId lit = _cfg.newLiteral(nullptr, *result);
							operation.kind = SSACFG::LiteralAssignment{};
							operation.inputs = {lit};
							changed = true;
							// Fall through to do CSE for this new LiteralAssignment.
						}
					}
				}

				// Build CSE key.
				CSEKey key;
				if (std::holds_alternative<SSACFG::LiteralAssignment>(operation.kind))
					key = CSEKey{nullptr, operation.inputs};
				else
					key = CSEKey{&builtin, operation.inputs};

				// Record this definition.
				defBlock[operation.outputs[0]] = blockIdx;

				// Global CSE: check for duplicate operations.
				auto it = globalCSE.find(key);
				if (it != globalCSE.end())
				{
					auto const& [existingVar, existingBlock] = it->second;
					// Only substitute if the existing definition dominates this use.
					if (dominates(existingBlock, blockIdx, idom))
					{
						substitutions[operation.outputs[0]] = existingVar;
						changed = true;
						continue;
					}
				}

				// No dominating duplicate found - record this as the canonical definition.
				// But only if no existing entry, or if this one dominates the existing one.
				if (it == globalCSE.end())
					globalCSE[key] = {operation.outputs[0], blockIdx};
				else if (dominates(blockIdx, it->second.second, idom))
					it->second = {operation.outputs[0], blockIdx};
			}
		}
	}

	if (substitutions.empty())
		return;

	// Phase 2: Apply substitutions globally to all ValueId uses.
	for (size_t blockIdx = 0; blockIdx < _cfg.numBlocks(); ++blockIdx)
	{
		SSACFG::BasicBlock& block = _cfg.block(SSACFG::BlockId{static_cast<SSACFG::BlockId::ValueType>(blockIdx)});

		for (auto& op: block.operations)
			for (auto& input: op.inputs)
				applySubstitution(substitutions, input);

		if (auto* cjump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&block.exit))
			applySubstitution(substitutions, cjump->condition);
		else if (auto* jtable = std::get_if<SSACFG::BasicBlock::JumpTable>(&block.exit))
			applySubstitution(substitutions, jtable->value);
		else if (auto* ret = std::get_if<SSACFG::BasicBlock::FunctionReturn>(&block.exit))
			for (auto& rv: ret->returnValues)
				applySubstitution(substitutions, rv);

		for (auto const& phiId: block.phis)
		{
			auto& phi = _cfg.phiInfo(phiId);
			for (auto& arg: phi.arguments)
				applySubstitution(substitutions, arg);
		}
	}

	// Phase 3: Remove dead operations whose outputs were substituted away.
	for (size_t blockIdx = 0; blockIdx < _cfg.numBlocks(); ++blockIdx)
	{
		SSACFG::BasicBlock& block = _cfg.block(SSACFG::BlockId{static_cast<SSACFG::BlockId::ValueType>(blockIdx)});
		std::erase_if(block.operations, [&](SSACFG::Operation const& _op) {
			return _op.outputs.size() == 1 && substitutions.contains(_op.outputs[0]);
		});
	}
}
