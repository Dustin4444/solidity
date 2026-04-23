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

#include <libyul/backends/evm/ssa/io/JSONExporter.h>

#include <libyul/Utilities.h>

#include <libsolutil/Algorithms.h>
#include <libsolutil/Numeric.h>
#include <libsolutil/Visitor.h>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::io::json;

namespace
{
Json toJson(SSACFG const& _cfg, std::vector<SSACFG::ValueId> const& _values)
{
	Json ret = Json::array();
	for (auto const& value: _values)
		ret.push_back(value.str(_cfg));
	return ret;
}

Json toJson(Json& _ret, SSACFG const& _cfg, SSACFG::InstId _instId, SSACFG::Inst const& _inst, ControlFlowGraphs const& _controlFlow)
{
	Json opJson = Json::object();
	if (_inst.opcode == Opcode::Call)
	{
		auto const& callPayload = _cfg.callPayload(_instId);
		_ret["type"] = "FunctionCall";
		opJson["op"] = _controlFlow.functionGraph(callPayload.graphID)->name;
	}
	else
	{
		yulAssert(_inst.opcode == Opcode::BuiltinCall);
		auto const& builtinPayload = _cfg.builtinPayload(_instId);
		_ret["type"] = "BuiltinCall";
		Json builtinArgsJson = Json::array();
		auto const& builtin = builtinPayload.builtin.get();
		if (!builtin.literalArguments.empty())
		{
			auto const& functionCallArgs = builtinPayload.call.get().arguments;
			for (size_t i = 0; i < builtin.literalArguments.size(); ++i)
			{
				std::optional<LiteralKind> const& argument = builtin.literalArguments[i];
				if (argument.has_value() && i < functionCallArgs.size())
				{
					// The function call argument at index i must be a literal if builtin.literalArguments[i] is not nullopt
					yulAssert(std::holds_alternative<Literal>(functionCallArgs[i]));
					builtinArgsJson.push_back(formatLiteral(std::get<Literal>(functionCallArgs[i])));
				}
			}
		}

		if (!builtinArgsJson.empty())
			opJson["literalArgs"] = builtinArgsJson;

		opJson["op"] = builtin.name;
	}

	opJson["in"] = toJson(_cfg, _inst.inputs);
	opJson["out"] = toJson(_cfg, _inst.outputs);

	return opJson;
}

Json toJson(SSACFG const& _cfg, SSACFG::BlockId _blockId, LivenessAnalysis const* _liveness, ControlFlowGraphs const& _controlFlow)
{
	auto const valueToString = [&](LivenessAnalysis::LivenessData::LiveCounts::value_type const& _live) { return _live.first.str(_cfg); };

	Json blockJson = Json::object();
	auto const& block = _cfg.block(_blockId);

	blockJson["id"] = "Block" + std::to_string(_blockId.value);
	if (_liveness)
	{
		Json livenessJson = Json::object();
		livenessJson["in"] = _liveness->liveIn(_blockId)
			| ranges::views::transform(valueToString)
			| ranges::to<Json::array_t>();
		livenessJson["out"] = _liveness->liveOut(_blockId)
			| ranges::views::transform(valueToString)
			| ranges::to<Json::array_t>();
		blockJson["liveness"] = livenessJson;
	}
	blockJson["instructions"] = Json::array();
	bool const hasPhis = ranges::any_of(block.instructions, [&](SSACFG::InstId id) {
		return _cfg.inst(id).opcode == Opcode::Phi;
	});
	if (hasPhis)
		blockJson["entries"] = block.entries
			| ranges::views::transform([](auto const& entry) { return "Block" + std::to_string(entry.value); })
			| ranges::to<Json::array_t>();
	for (SSACFG::InstId const instId: block.instructions)
	{
		auto const& inst = _cfg.inst(instId);
		if (inst.opcode != Opcode::Phi)
			continue;
		SSACFG::ValueId const phi = inst.outputs.at(0);
		// Reconstruct phi arguments from upsilon Insts in predecessor blocks.
		std::vector<SSACFG::ValueId> phiArgs;
		for (auto const& entryId: block.entries)
			for (SSACFG::InstId const predInstId: _cfg.block(entryId).instructions)
			{
				auto const& predInst = _cfg.inst(predInstId);
				if (predInst.opcode != Opcode::Upsilon)
					continue;
				if (_cfg.upsilonPhi(predInstId) == phi)
				{
					phiArgs.push_back(predInst.inputs.at(0));
					break;
				}
			}
		Json phiJson = Json::object();
		phiJson["op"] = "PhiFunction";
		phiJson["in"] = toJson(_cfg, phiArgs);
		phiJson["out"] = toJson(_cfg, std::vector{phi});
		blockJson["instructions"].push_back(phiJson);
	}
	for (SSACFG::InstId const instId: block.instructions)
	{
		auto const& inst = _cfg.inst(instId);
		if (inst.opcode != Opcode::BuiltinCall && inst.opcode != Opcode::Call)
			continue;
		blockJson["instructions"].push_back(toJson(blockJson, _cfg, instId, inst, _controlFlow));
	}

	return blockJson;
}

Json exportBlock(SSACFG const& _cfg, SSACFG::BlockId _entryId, LivenessAnalysis const* _liveness, ControlFlowGraphs const& _controlFlow)
{
	Json blocksJson = Json::array();
	util::BreadthFirstSearch<SSACFG::BlockId> bfs{{{_entryId}}};
	bfs.run([&](SSACFG::BlockId _blockId, auto _addChild) {
		Json blockJson = toJson(_cfg, _blockId, _liveness, _controlFlow);

		Json exitBlockJson = Json::object();
		std::visit(util::GenericVisitor{
			[&](SSACFG::BasicBlock::MainExit const&) {
				exitBlockJson["type"] = "MainExit";
			},
			[&](SSACFG::BasicBlock::Jump const& _jump){
				exitBlockJson["targets"] = { "Block" + std::to_string(_jump.target.value) };
				exitBlockJson["type"] = "Jump";
				_addChild(_jump.target);
			},
			[&](SSACFG::BasicBlock::ConditionalJump const& _conditionalJump) {
				exitBlockJson["targets"] = { "Block" + std::to_string(_conditionalJump.zero.value), "Block" + std::to_string(_conditionalJump.nonZero.value) };
				exitBlockJson["cond"] = _conditionalJump.condition.str(_cfg);
				exitBlockJson["type"] = "ConditionalJump";

				_addChild(_conditionalJump.zero);
				_addChild(_conditionalJump.nonZero);
			},
			[&](SSACFG::BasicBlock::FunctionReturn const& _return) {
				exitBlockJson["returnValues"] = toJson(_cfg, _return.returnValues);
				exitBlockJson["type"] = "FunctionReturn";
			},
			[&](SSACFG::BasicBlock::Terminated const&) {
				exitBlockJson["type"] = "Terminated";
			}
		}, _cfg.block(_blockId).exit);
		blockJson["exit"] = exitBlockJson;
		blocksJson.emplace_back(blockJson);
	});

	return blocksJson;
}

Json exportFunction(SSACFG const& _cfg, LivenessAnalysis const* _liveness, ControlFlowGraphs const& _controlFlow)
{
	Json functionJson = Json::object();
	functionJson["type"] = "Function";
	functionJson["entry"] = "Block" + std::to_string(_cfg.entry.value);
	static auto constexpr argsTransform = [](auto const& _arg) { return fmt::format("v{}", _arg.instIdx()); };
	functionJson["arguments"] = _cfg.arguments | ranges::views::transform(argsTransform) | ranges::to<std::vector>;
	functionJson["numReturns"] = _cfg.numReturns;
	functionJson["blocks"] = exportBlock(_cfg, _cfg.entry, _liveness, _controlFlow);
	return functionJson;
}

}

Json io::json::exportControlFlow(ControlFlowGraphs const& _controlFlow, ControlFlowGraphsLiveness const* _liveness)
{
	if (_liveness)
		yulAssert(&_liveness->controlFlowGraphs.get() == &_controlFlow);

	Json yulObjectJson = Json::object();
	yulObjectJson["blocks"] = exportBlock(
		*_controlFlow.mainGraph(),
		SSACFG::BlockId{0},
		_liveness ? _liveness->cfgLiveness.front().get() : nullptr,
		_controlFlow
	);

	Json functionsJson = Json::object();
	for (std::size_t index = 1; index < _controlFlow.functionGraphs.size(); ++index)
	{
		SSACFG const& functionGraph = *_controlFlow.functionGraphs[index];
		functionsJson[functionGraph.name] = exportFunction(
			functionGraph,
			_liveness ? _liveness->cfgLiveness[index].get() : nullptr,
			_controlFlow
		);
	}
	yulObjectJson["functions"] = functionsJson;

	return yulObjectJson;
}
