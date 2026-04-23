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

#include <libyul/backends/evm/ssa/transform/TrivialPhiEliminator.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/count_if.hpp>
#include <range/v3/algorithm/replace.hpp>

#include <algorithm>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

struct TrivialPhiEliminator
{
	SSACFG& cfg;

	// phi_id -> values of upsilons targeting this phi
	std::vector<std::vector<SSACFG::ValueId>> upsilonValuesForPhi;
	// phi_id -> blocks containing upsilons targeting this phi
	std::vector<std::vector<SSACFG::BlockId>> phiTargetBlocks;
	// phi_id -> phis whose upsilon values reference this phi
	std::vector<std::vector<SSACFG::ValueId>> reversePhiUpsilonValues;
	// phi_id -> canonical replacement (default = no substitution)
	std::vector<SSACFG::ValueId> substitution;

	explicit TrivialPhiEliminator(SSACFG& _cfg): cfg(_cfg) {}

	void run()
	{
		cleanUnreachableUpsilons();
		buildIndices();
		substituteDeadPhis();

		// run elimination repeatedly until convergence
		auto substitutionsBeforeEliminateAll = static_cast<std::size_t>(ranges::count_if(substitution, [](auto const& s) { return s.hasValue(); }));
		auto substitutionsAfterEliminateAll = substitutionsBeforeEliminateAll;
		do
		{
			substitutionsBeforeEliminateAll = substitutionsAfterEliminateAll;
			eliminateAll();
			substitutionsAfterEliminateAll = static_cast<std::size_t>(ranges::count_if(substitution, [](auto const& s) { return s.hasValue(); }));
		} while (substitutionsAfterEliminateAll > substitutionsBeforeEliminateAll);
		applySubstitutions();
	}

private:
	void cleanUnreachableUpsilons()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			auto& block = cfg.block(blockId);
			std::erase_if(block.instructions, [this](SSACFG::InstId id) {
				auto const& ins = cfg.inst(id);
				return ins.opcode == Opcode::Upsilon && ins.inputs.at(0).isUnreachable();
			});
		}
	}

	void buildIndices()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			auto const& block = cfg.block(blockId);
			for (SSACFG::InstId const instId: block.instructions)
			{
				auto const& inst = cfg.inst(instId);
				if (inst.opcode != Opcode::Upsilon)
					continue;
				SSACFG::ValueId const phi = cfg.upsilonPhi(instId);
				SSACFG::ValueId const value = inst.inputs.at(0);
				auto const phiIdx = phi.instIdx();
				if (phiIdx >= upsilonValuesForPhi.size())
				{
					upsilonValuesForPhi.resize(phiIdx + 1);
					phiTargetBlocks.resize(phiIdx + 1);
				}
				if (phiIdx >= reversePhiUpsilonValues.size())
					reversePhiUpsilonValues.resize(phiIdx + 1);
				upsilonValuesForPhi[phiIdx].push_back(value);
				phiTargetBlocks[phiIdx].push_back(blockId);
				if (value.isPhi())
				{
					auto const valIdx = value.instIdx();
					if (valIdx >= reversePhiUpsilonValues.size())
						reversePhiUpsilonValues.resize(valIdx + 1);
					reversePhiUpsilonValues[valIdx].push_back(phi);
				}
			}
		}
		substitution.resize(std::max(upsilonValuesForPhi.size(), size_t{1}));
	}

	/// Phis that have no upsilons targeting them are dead — either in unreachable blocks
	/// or orphaned from cycle-breaking.
	/// Replace references to them with unreachableValue in the upsilon value index so they don't block triviality
	/// detection.
	void substituteDeadPhis()
	{
		auto const unreachable = cfg.unreachableValue();
		for (auto& upsilonValues: upsilonValuesForPhi)
			for (auto& val: upsilonValues)
				if (val.isPhi())
				{
					auto const idx = val.instIdx();
					bool const hasUpsilons = idx < upsilonValuesForPhi.size() && !upsilonValuesForPhi[idx].empty();
					if (!hasUpsilons)
						val = unreachable;
				}
	}

	void eliminateAll()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			// Snapshot the phi ValueIds because tryRemove mutates block.instructions.
			std::vector<SSACFG::ValueId> phis;
			for (SSACFG::InstId const instId: cfg.block(blockId).instructions)
			{
				auto const& inst = cfg.inst(instId);
				if (inst.opcode == Opcode::Phi)
					phis.push_back(inst.outputs.at(0));
			}
			for (auto const phi: phis)
				tryRemove(phi);
		}
	}

	void tryRemove(SSACFG::ValueId _phi)
	{
		auto const phiIdx = _phi.instIdx();

		// early exit if this phi was already processed
		if (phiIdx < substitution.size() && substitution[phiIdx].hasValue())
			return;

		// check triviality and canonicalize arguments to account for already-substituted phis;
		// skip self-references and unreachable values
		SSACFG::ValueId same;
		if (phiIdx < upsilonValuesForPhi.size())
			for (auto const arg: upsilonValuesForPhi[phiIdx])
			{
				auto const resolved = canonicalize(arg);
				if (resolved == same || resolved == _phi || resolved.isUnreachable())
					continue;
				if (same.hasValue())
					return; // non-trivial
				same = resolved;
			}

		if (!same.hasValue())
			// phi has only self-references (unreachable cycle), or no upsilons at all
			same = cfg.unreachableValue();

		// remove the phi from its defining block
		{
			auto& block = cfg.block(cfg.inst(_phi.instId()).block);
			std::erase_if(block.instructions, [this, _phi](SSACFG::InstId id) {
				auto const& ins = cfg.inst(id);
				return ins.opcode == Opcode::Phi && !ins.outputs.empty() && ins.outputs.front() == _phi;
			});
		}

		// record the substitution
		if (phiIdx >= substitution.size())
			substitution.resize(phiIdx + 1);
		substitution[phiIdx] = same;

		// update upsilon.value fields and `upsilonValuesForPhi` for all phis whose upsilons reference `_phi` as a value
		std::vector<SSACFG::ValueId> cascades;
		if (phiIdx < reversePhiUpsilonValues.size())
		{
			for (auto const targetPhi: reversePhiUpsilonValues[phiIdx])
			{
				// update upsilonValuesForPhi[targetPhi]: _phi with same
				auto& vals = upsilonValuesForPhi[targetPhi.instIdx()];
				ranges::replace(vals, _phi, same);
				if (targetPhi.instIdx() < phiTargetBlocks.size())
					for (auto const blockId: phiTargetBlocks[targetPhi.instIdx()])
					{
						auto& block = cfg.block(blockId);
						for (SSACFG::InstId const instId: block.instructions)
						{
							auto& ins = cfg.inst(instId);
							if (ins.opcode == Opcode::Upsilon
								&& cfg.upsilonPhi(instId) == targetPhi
								&& ins.inputs.at(0) == _phi)
								ins.inputs[0] = same;
						}
					}
				// maintain reverse index
				if (same.isPhi())
				{
					if (same.instIdx() >= reversePhiUpsilonValues.size())
						reversePhiUpsilonValues.resize(same.instIdx() + 1);
					reversePhiUpsilonValues[same.instIdx()].push_back(targetPhi);
				}
				cascades.push_back(targetPhi);
			}
			reversePhiUpsilonValues[phiIdx].clear();
		}

		// erase all upsilons targeting the removed phi.
		if (phiIdx < phiTargetBlocks.size())
		{
			for (auto const blockId: phiTargetBlocks[phiIdx])
			{
				auto& block = cfg.block(blockId);
				std::erase_if(block.instructions, [this, _phi](SSACFG::InstId id) {
					auto const& ins = cfg.inst(id);
					return ins.opcode == Opcode::Upsilon && cfg.upsilonPhi(id) == _phi;
				});
			}
			phiTargetBlocks[phiIdx].clear();
		}

		for (auto const cascadingPhi: cascades)
			tryRemove(cascadingPhi);
	}

	SSACFG::ValueId canonicalize(SSACFG::ValueId const _v)
	{
		if (!_v.isPhi())
			return _v;
		auto const idx = _v.instIdx();
		if (idx >= substitution.size())
			return _v;
		auto& sub = substitution[idx];
		if (!sub.hasValue())
			return _v;
		// path compression
		sub = canonicalize(sub);
		return sub;
	}

	void applySubstitutions()
	{
		if (!ranges::any_of(substitution, [](ValueId const& _sub) { return _sub.hasValue(); }))
			return;

		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			auto& block = cfg.block(blockId);
			for (SSACFG::InstId const instId: block.instructions)
			{
				auto& ins = cfg.inst(instId);
				if (ins.opcode == Opcode::Upsilon)
					ins.inputs[0] = canonicalize(ins.inputs[0]);
				else if (ins.opcode == Opcode::BuiltinCall || ins.opcode == Opcode::Call)
					for (auto& input: ins.inputs)
						input = canonicalize(input);
			}
			std::visit(util::GenericVisitor{
				[&](SSACFG::BasicBlock::FunctionReturn& r) {
					for (auto& v: r.returnValues)
						v = canonicalize(v);
				},
				[&](SSACFG::BasicBlock::ConditionalJump& c) { c.condition = canonicalize(c.condition); },
				[](SSACFG::BasicBlock::Jump&) {},
				[](SSACFG::BasicBlock::MainExit&) {},
				[](SSACFG::BasicBlock::Terminated&) {}
			}, block.exit);
		}
	}
};

}

void transform::eliminateTrivialPhis(SSACFG& _cfg)
{
	TrivialPhiEliminator(_cfg).run();
}
