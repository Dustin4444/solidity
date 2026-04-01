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

#include <libyul/backends/evm/ssa/traversal/DominatorTree.h>

#include <libyul/backends/evm/ssa/traversal/ForwardTopologicalSort.h>

#include <range/v3/view/iota.hpp>
#include <range/v3/to_container.hpp>

#include <algorithm>
#include <limits>

using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::traversal;

namespace
{
constexpr std::size_t NONE = std::numeric_limits<std::size_t>::max();
}

DominatorTree::DominatorTree(ForwardTopologicalSort const& _sort)
{
	SSACFG const& cfg = _sort.cfg();
	auto const& preOrder = _sort.preOrder();
	std::size_t const numConnectedBlocks = preOrder.size();
	std::size_t const numBlocks = cfg.numBlocks();
	auto const entryValue = cfg.entry.value;

	constexpr auto undefinedBlock = std::numeric_limits<SSACFG::BlockId::ValueType>::max();
	m_idom.assign(numBlocks, undefinedBlock);
	m_domTreePreOrder.assign(numBlocks, 0);
	m_domTreeMaxSubtreePreOrder.assign(numBlocks, 0);

	if (numConnectedBlocks <= 1)
	{
		m_idom[entryValue] = entryValue;
		return;
	}

	// Reconstruct DFS tree parents from the topological sort
	// For each node in preorder, the parent is the top of a stack of open ancestors.
	std::vector<std::size_t> parent(numConnectedBlocks, 0);
	{
		std::vector<std::size_t> stack;
		stack.reserve(numConnectedBlocks);
		stack.push_back(0);
		// todo is this right? why is there no 'visited' check?
		for (std::size_t i = 1; i < numConnectedBlocks; ++i)
		{
			while (!_sort.ancestor(preOrder[stack.back()], preOrder[i]))
				stack.pop_back();
			parent[i] = stack.back();
			stack.push_back(i);
		}
	}

	// --- Lengauer-Tarjan algorithm ---
	// All arrays are indexed by DFS index (0 = entry).

	std::vector semi = ranges::views::iota(0u, numConnectedBlocks) | ranges::to<std::vector<std::size_t>>();
	std::vector label = semi;
	std::vector idom(numConnectedBlocks, NONE);
	std::vector forestAncestor(numConnectedBlocks, NONE);
	std::vector<std::vector<std::size_t>> bucket(numConnectedBlocks);

	auto eval = [&](std::size_t v) -> std::size_t {
		if (forestAncestor[v] == NONE)
			return v;
		compressPath(forestAncestor, label, semi, v);
		return label[v];
	};

	// Process vertices in reverse DFS preorder (innermost first), skipping entry.
	for (std::size_t w = numConnectedBlocks - 1; w >= 1; --w)
	{
		// Step 3 (optimized: process bucket[w] before step 2, see Georgiadis & Tarjan, JGAA 2004).
		for (std::size_t const v: bucket[w])
		{
			std::size_t u = eval(v);
			idom[v] = semi[u] < semi[v] ? u : w;
		}

		// Step 2: compute semidominator of w.
		for (auto const& predBlock: cfg.block({preOrder[w]}).entries)
		{
			auto const predDfsIdx = _sort.preOrderIndexOf(predBlock.value);
			if (predDfsIdx < numConnectedBlocks && preOrder[predDfsIdx] == predBlock.value)
			{
				std::size_t const u = eval(predDfsIdx);
				if (semi[u] < semi[w])
					semi[w] = semi[u];
			}
		}
		bucket[semi[w]].push_back(w);
		forestAncestor[w] = parent[w];
	}

	// Handle nodes whose semidominator is the entry (bucket[0]).
	for (std::size_t const v: bucket[0])
		idom[v] = 0;

	// Step 4: propagate immediate dominators.
	idom[0] = 0;
	for (std::size_t w = 1; w < numConnectedBlocks; ++w)
		if (idom[w] != semi[w])
			idom[w] = idom[idom[w]];

	// Convert idom from DFS index space to block ID space.
	for (std::size_t i = 0; i < numConnectedBlocks; ++i)
		m_idom[preOrder[i]] = preOrder[idom[i]];

	// Build dominator tree children lists.
	std::vector<std::vector<SSACFG::BlockId::ValueType>> children(numBlocks);
	for (std::size_t i = 1; i < numConnectedBlocks; ++i)
		children[m_idom[preOrder[i]]].push_back(preOrder[i]);

	// DFS on dominator tree to compute preorder + max subtree preorder `dominates` check
	std::size_t counter = 0;
	std::vector<std::pair<SSACFG::BlockId::ValueType, std::size_t>> dfsStack;
	dfsStack.reserve(numConnectedBlocks);
	dfsStack.emplace_back(entryValue, 0);
	m_domTreePreOrder[entryValue] = counter++;

	while (!dfsStack.empty())
	{
		auto& [node, childIdx] = dfsStack.back();
		if (childIdx < children[node].size())
		{
			auto child = children[node][childIdx++];
			m_domTreePreOrder[child] = counter++;
			m_domTreeMaxSubtreePreOrder[child] = m_domTreePreOrder[child];
			dfsStack.emplace_back(child, 0);
		}
		else
		{
			auto maxPre = m_domTreeMaxSubtreePreOrder[node];
			dfsStack.pop_back();
			if (!dfsStack.empty())
				m_domTreeMaxSubtreePreOrder[dfsStack.back().first] = std::max(
					m_domTreeMaxSubtreePreOrder[dfsStack.back().first], maxPre
				);
		}
	}
}

void DominatorTree::compressPath(
	std::vector<std::size_t>& _ancestor,
	std::vector<std::size_t>& _label,
	std::vector<std::size_t> const& _semi,
	std::size_t const _v
)
{
	if (_ancestor[_ancestor[_v]] != NONE)
	{
		compressPath(_ancestor, _label, _semi, _ancestor[_v]);
		if (_semi[_label[_ancestor[_v]]] < _semi[_label[_v]])
			_label[_v] = _label[_ancestor[_v]];
		_ancestor[_v] = _ancestor[_ancestor[_v]];
	}
}

bool DominatorTree::dominates(SSACFG::BlockId _dominator, SSACFG::BlockId _dominated) const
{
	return
		m_domTreePreOrder[_dominator.value] <= m_domTreePreOrder[_dominated.value] &&
		m_domTreePreOrder[_dominated.value] <= m_domTreeMaxSubtreePreOrder[_dominator.value];
}

SSACFG::BlockId DominatorTree::immediateDominator(SSACFG::BlockId _block) const
{
	return SSACFG::BlockId{m_idom[_block.value]};
}
