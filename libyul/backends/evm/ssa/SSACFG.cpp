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

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/ssa/JunkAdmittingBlocksFinder.h>
#include <libyul/backends/evm/ssa/io/DotExporterBase.h>

#include <libsolutil/StringUtils.h>
#include <libsolutil/Visitor.h>

#include <fmt/ranges.h>

#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

/// Build a human-readable Phi/Upsilon annotation for a phi value.
/// Shows which upsilons feed it, listed per predecessor block.
std::string formatPhi(SSACFG const& _cfg, SSACFG::ValueId _phiId)
{
	// Collect all upsilons targeting _phiId from the whole CFG.
	std::vector<std::string> formattedUpsilons;
	for (SSACFG::BlockId::ValueType bv = 0; bv < _cfg.numBlocks(); ++bv)
		for (SSACFG::InstId const instId: _cfg.block(SSACFG::BlockId{bv}).instructions)
		{
			auto const& inst = _cfg.inst(instId);
			if (inst.opcode != Opcode::Upsilon)
				continue;
			if (_cfg.upsilonPhi(instId) == _phiId)
				formattedUpsilons.push_back(
					fmt::format("Block {} => {}", bv, inst.inputs.at(0).str(_cfg))
				);
		}
	if (!formattedUpsilons.empty())
		return fmt::format("φ(\\l\\\n\t{}\\l\\\n)", fmt::join(formattedUpsilons, ",\\l\\\n\t"));
	return "φ()";
}

class SSACFGDotExporter: public io::DotExporterBase
{
public:
	SSACFGDotExporter(SSACFG const& _cfg, size_t _functionIndex, LivenessAnalysis const* _liveness, ControlFlowGraphs const* _controlFlow):
		DotExporterBase(_cfg, _functionIndex),
		m_liveness(_liveness),
		m_controlFlow(_controlFlow)
	{
		if (_liveness)
			m_junkAdmittingBlocks = std::make_unique<JunkAdmittingBlocksFinder>(_cfg, _liveness->topologicalSort());
	}

protected:
	void writeBlockLabel(std::ostream& _out, SSACFG::BlockId _blockId) override
	{
		auto const& block = m_cfg.block(_blockId);
		auto const valueToString = [&](SSACFG::ValueId const& valueId) { return valueId.str(m_cfg); };

		if (m_liveness)
		{
			_out << fmt::format(
				"\\\nBlock {}; ({}, max {})\\n",
				_blockId.value,
				m_liveness->topologicalSort().preOrderIndexOf(_blockId.value),
				m_liveness->topologicalSort().maxSubtreePreOrderIndexOf(_blockId.value)
			);
			_out << fmt::format(
				"LiveIn: {}\\l\\\n",
				fmt::join(m_liveness->liveIn(_blockId) | ranges::views::transform([&](auto const& liveIn) { return valueToString(SSACFG::ValueId{liveIn.first}) + fmt::format("[{}]", liveIn.second); }), ", ")
			);
			_out << fmt::format(
				"LiveOut: {}\\l\\n",
				fmt::join(m_liveness->liveOut(_blockId) | ranges::views::transform([&](auto const& liveOut) { return valueToString(SSACFG::ValueId{liveOut.first}) + fmt::format("[{}]", liveOut.second); }), ", ")
			);
			auto const usedVariables = m_liveness->used(_blockId);
			_out << fmt::format(
				"Used: {}\\l\\n",
				fmt::join(usedVariables | ranges::views::transform([&](auto const& used) { return valueToString(SSACFG::ValueId{used.first}) + fmt::format("[{}]", used.second); }), ", ")
			);
		}
		else
			_out << fmt::format("\\\nBlock {}\\n", _blockId.value);

		// Phis first, then BuiltinCall / Call in program order. Upsilons are rendered
		// under successor phis (via formatPhi) and skipped here.
		for (SSACFG::InstId const instId: block.instructions)
		{
			auto const& inst = m_cfg.inst(instId);
			if (inst.opcode != Opcode::Phi)
				continue;
			SSACFG::ValueId const phi = inst.outputs.at(0);
			_out << fmt::format("phi{} := {}\\l\\\n", phi.value(), formatPhi(m_cfg, phi));
		}
		for (SSACFG::InstId const instId: block.instructions)
		{
			auto const& inst = m_cfg.inst(instId);
			if (inst.opcode != Opcode::BuiltinCall && inst.opcode != Opcode::Call)
				continue;
			std::string label;
			if (inst.opcode == Opcode::Call)
			{
				auto const graphID = m_cfg.callPayload(instId).graphID;
				label = m_controlFlow ? m_controlFlow->functionGraph(graphID)->name : fmt::format("func{}", graphID);
			}
			else
				label = m_cfg.builtinPayload(instId).builtin.get().name;
			if (!inst.outputs.empty())
				_out << fmt::format(
					"{} := ",
					fmt::join(inst.outputs | ranges::views::transform(valueToString), ", ")
				);
			_out << fmt::format(
				"{}({})\\l\\\n",
				escapeLabel(label),
				fmt::join(inst.inputs | ranges::views::transform(valueToString), ", ")
			);
		}
	}

	std::vector<std::pair<std::string, std::string>> blockNodeAttributes(SSACFG::BlockId _blockId) override
	{
		if (m_junkAdmittingBlocks && m_junkAdmittingBlocks->allowsAdditionOfJunk(_blockId))
			return {{"fillcolor", "\"#FF746C\""}, {"style", "filled"}};
		return {};
	}

	EdgeStyle edgeStyle(SSACFG::BlockId _source, SSACFG::BlockId _target) override
	{
		if (m_liveness && m_liveness->topologicalSort().backEdge(_source, _target))
			return EdgeStyle::Dashed;
		return EdgeStyle::Solid;
	}

private:
	LivenessAnalysis const* m_liveness;
	ControlFlowGraphs const* m_controlFlow;
	std::unique_ptr<JunkAdmittingBlocksFinder> m_junkAdmittingBlocks;
};

}

std::string ValueId::str(SSACFG const& _cfg) const
{
	if (!hasValue())
		return "INVALID";
	switch (kind())
	{
		case Kind::Literal:  return toCompactHexWithPrefix(_cfg.literalInfo(*this).value);
		case Kind::Variable: return fmt::format("v{}", value());
		case Kind::Phi: return fmt::format("phi{}", value());
		case Kind::Unreachable: return "[unreachable]";
	}
	unreachable();
}


std::string SSACFG::toDot(
	bool _includeDiGraphDefinition,
	std::optional<size_t> _functionIndex,
	LivenessAnalysis const* _liveness,
	ControlFlowGraphs const* _controlFlow
) const
{
	SSACFGDotExporter exporter(*this, _functionIndex.value_or(isMainGraph() ? 0 : 1), _liveness, _controlFlow);
	if (!isMainGraph())
		return exporter.exportFunction(*this, _includeDiGraphDefinition);
	else
		return exporter.exportBlocks(entry, _includeDiGraphDefinition);
}

Opcode SSACFG::kindOf(ValueId _v) const
{
	yulAssert(_v.hasValue());
	switch (_v.kind())
	{
	case ValueId::Kind::Literal:     return Opcode::Const;
	case ValueId::Kind::Phi:         return Opcode::Phi;
	case ValueId::Kind::Variable:    return Opcode::BuiltinCall;  // bridge: variable ValueIds originate from BuiltinCall/Call Insts; distinguished later via m_insts
	case ValueId::Kind::Unreachable: return Opcode::Unreachable;
	}
	unreachable();
}
