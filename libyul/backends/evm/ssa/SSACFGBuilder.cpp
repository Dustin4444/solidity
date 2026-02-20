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
 * Transformation of a Yul AST into a control flow graph.
 */

#include <libyul/backends/evm/ssa/SSACFGBuilder.h>

#include <libyul/backends/evm/ssa/ControlFlow.h>

#include <chrono>

#include <libyul/AST.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/ControlFlowSideEffectsCollector.h>
#include <libyul/Exceptions.h>
#include <libyul/Utilities.h>

#include <libsolutil/Algorithms.h>
#include <libsolutil/StringUtils.h>
#include <libsolutil/Visitor.h>

#include <range/v3/algorithm/replace.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/drop_last.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

SSACFGBuilder::Stats SSACFGBuilder::s_stats{};

SSACFGBuilder::SSACFGBuilder(
	ControlFlow& _controlFlow,
	SSACFG& _graph,
	AsmAnalysisInfo const& _analysisInfo,
	ControlFlowSideEffectsCollector const& _sideEffects,
	Dialect const& _dialect,
	bool _keepLiteralAssignments,
	bool _includeDebugData
):
	m_controlFlow(_controlFlow),
	m_graph(_graph),
	m_info(_analysisInfo),
	m_sideEffects(_sideEffects),
	m_dialect(_dialect),
	m_keepLiteralAssignments(_keepLiteralAssignments),
	m_includeDebugData(_includeDebugData)
{
}

std::unique_ptr<ControlFlow> SSACFGBuilder::build(
	AsmAnalysisInfo const& _analysisInfo,
	Dialect const& _dialect,
	Block const& _block,
	bool _keepLiteralAssignments,
	bool _includeDebugData
)
{
	clearStats();
	ControlFlowSideEffectsCollector sideEffects(_dialect, _block);

	auto controlFlow = std::make_unique<ControlFlow>();
	controlFlow->functionGraphs.emplace_back(std::make_unique<SSACFG>());
	controlFlow->functionGraphMapping.emplace_back(nullptr, controlFlow->functionGraphs.back().get());
	SSACFG& mainGraph = *controlFlow->functionGraphs.back();
	if (_includeDebugData)
		mainGraph.enableDebugData();
	SSACFGBuilder builder(*controlFlow, mainGraph, _analysisInfo, sideEffects, _dialect, _keepLiteralAssignments, _includeDebugData);
	builder.m_currentBlock = mainGraph.makeBlock(debugDataOf(_block));
	builder.sealBlock(builder.m_currentBlock);
	builder(_block);
	if (!builder.blockInfo(builder.m_currentBlock).sealed)
		builder.sealBlock(builder.m_currentBlock);
	mainGraph.block(builder.m_currentBlock).exit = SSACFG::BasicBlock::MainExit{};
	controlFlow->functionGraphs.at(0)->block(builder.m_currentBlock).exit = SSACFG::BasicBlock::MainExit{};
	builder.cleanUnreachable();
	return controlFlow;
}

SSACFG::ValueId SSACFGBuilder::tryRemoveTrivialPhi(SSACFG::ValueId _phi)
{
	auto const t0 = std::chrono::steady_clock::now();
	auto const& phiInfo = m_graph.phiInfo(_phi);
	yulAssert(blockInfo(phiInfo.block).sealed);

	++s_stats.tryRemoveCalls;

	// Check triviality via the index: O(upsilons for this phi) rather than
	// O(upsilons across all predecessor blocks).
	SSACFG::ValueId same;
	auto const phiIdx = _phi.value();
	if (phiIdx < m_upsilonValuesForPhi.size())
		for (auto const arg: m_upsilonValuesForPhi[phiIdx])
		{
			++s_stats.trivialCheckIterations;
			if (arg == same || arg == _phi)
				continue;  // duplicate or self-reference
			if (same.hasValue())
				return _phi;  // two distinct values -> non-trivial
			same = arg;
		}

	if (!same.hasValue())
		// Phi has only self-references (unreachable cycle), or no upsilons at all.
		same = m_graph.unreachableValue();

	++s_stats.tryRemoveSucceeded;

	// Remove the phi from its defining block.
	std::erase(m_graph.block(phiInfo.block).phis, _phi);

	// Replace all uses of _phi with same throughout the graph.
	// Collect phis whose upsilons now reference `same` (they may become trivial).
	std::vector<SSACFG::ValueId> phiUses;
	for (SSACFG::BlockId blockId{0}; blockId.value < m_graph.numBlocks(); ++blockId.value)
	{
		++s_stats.replacementBlocksScanned;
		auto& block = m_graph.block(blockId);

		// Pass 1: fix value references in upsilons targeting other phis, maintaining the index.
		for (auto& upsilon: block.upsilons)
		{
			++s_stats.replacementUpsilonsScanned;
			if (upsilon.value == _phi)
			{
				upsilon.value = same;
				// Update index: replace the _phi entry with same for the target phi.
				auto& vals = m_upsilonValuesForPhi[upsilon.phi.value()];
				auto const it = std::find(vals.begin(), vals.end(), _phi);
				if (it != vals.end())
					*it = same;
				phiUses.push_back(upsilon.phi);
			}
		}
		// Pass 2: drop upsilons targeting _phi (in-place, no allocation).
		std::erase_if(block.upsilons, [_phi](auto const& u) { return u.phi == _phi; });
		// m_upsilonValuesForPhi[phiIdx] is now stale but unreachable: _phi is gone and
		// no new upsilons will ever target it, so it will never be consulted again.

		for (auto& op: block.operations)
		{
			s_stats.replacementOpInputsScanned += op.inputs.size();
			ranges::replace(op.inputs, _phi, same);
		}
		std::visit(util::GenericVisitor{
			[_phi, same](SSACFG::BasicBlock::FunctionReturn& _functionReturn) {
				ranges::replace(_functionReturn.returnValues, _phi, same);
			},
			[_phi, same](SSACFG::BasicBlock::ConditionalJump& _condJump) {
				if (_condJump.condition == _phi)
					_condJump.condition = same;
			},
			[_phi, same](SSACFG::BasicBlock::JumpTable& _jumpTable) {
				if (_jumpTable.value == _phi)
					_jumpTable.value = same;
			},
			[](SSACFG::BasicBlock::Jump&) {},
			[](SSACFG::BasicBlock::MainExit&) {},
			[](SSACFG::BasicBlock::Terminated&) {}
		}, block.exit);
	}
	for (auto& currentVariableDefs: m_currentDef | ranges::views::values)
		ranges::replace(currentVariableDefs, _phi, same);

	for (auto phiUse: phiUses)
		tryRemoveTrivialPhi(phiUse);

	s_stats.tryRemoveNs += static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - t0
		).count()
	);
	return same;
}

/// Removes edges to blocks that are not reachable.
void SSACFGBuilder::cleanUnreachable()
{
	auto const t0_clean = std::chrono::steady_clock::now();
	// Determine which blocks are reachable from the entry.
	util::BreadthFirstSearch<SSACFG::BlockId> reachabilityCheck{{m_graph.entry}};
	reachabilityCheck.run([&](SSACFG::BlockId _blockId, auto&& _addChild) {
		auto const& block = m_graph.block(_blockId);
		visit(util::GenericVisitor{
				[&](SSACFG::BasicBlock::Jump const& _jump) {
					_addChild(_jump.target);
				},
				[&](SSACFG::BasicBlock::ConditionalJump const& _jump) {
					_addChild(_jump.zero);
					_addChild(_jump.nonZero);
				},
				[](SSACFG::BasicBlock::JumpTable const&) { yulAssert(false); },
				[](SSACFG::BasicBlock::FunctionReturn const&) {},
				[](SSACFG::BasicBlock::Terminated const&) {},
				[](SSACFG::BasicBlock::MainExit const&) {}
			}, block.exit);
	});

	// Remove unreachable predecessor entries.
	for (SSACFG::BlockId blockId: reachabilityCheck.visited)
	{
		auto& block = m_graph.block(blockId);
		std::erase_if(block.entries, [&](auto const& entry) {
			return !reachabilityCheck.visited.contains(entry);
		});
	}

	// Remove upsilons that are now invalid:
	//   - upsilons in unreachable blocks (their block will never execute), or
	//   - upsilons with an unreachable value (product of earlier trivial-phi removal).
	// Collect the affected target phis so we can attempt trivial-phi removal afterward.
	// Also remove the corresponding entries from the index to keep it consistent.
	std::vector<SSACFG::ValueId> maybeTrivialPhi;
	for (SSACFG::BlockId blockId{0}; blockId.value < m_graph.numBlocks(); ++blockId.value)
	{
		auto& block = m_graph.block(blockId);
		bool const isReachable = reachabilityCheck.visited.contains(blockId);

		for (auto const& u: block.upsilons)
			if (!isReachable || u.value.isUnreachable())
			{
				++s_stats.cleanUnreachableRemovals;
				maybeTrivialPhi.push_back(u.phi);
				auto const phiIdx = u.phi.value();
				if (phiIdx < m_upsilonValuesForPhi.size())
				{
					auto& vals = m_upsilonValuesForPhi[phiIdx];
					auto const it = std::find(vals.begin(), vals.end(), u.value);
					if (it != vals.end())
						vals.erase(it);
				}
			}

		std::erase_if(block.upsilons, [&](SSACFG::Upsilon const& u) {
			return !isReachable || u.value.isUnreachable();
		});
	}

	for (auto const phi: maybeTrivialPhi)
		tryRemoveTrivialPhi(phi);

	s_stats.cleanUnreachableNs += static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - t0_clean
		).count()
	);
}

void SSACFGBuilder::buildFunctionGraph(
	Scope::Function const* _function,
	FunctionDefinition const* _functionDefinition
)
{
	m_controlFlow.functionGraphs.emplace_back(std::make_unique<SSACFG>());
	auto& cfg = *m_controlFlow.functionGraphs.back();
	if (m_includeDebugData)
		cfg.enableDebugData();
	m_controlFlow.functionGraphMapping.emplace_back(_function, &cfg);

	yulAssert(m_info.scopes.at(&_functionDefinition->body), "");
	Scope* virtualFunctionScope = m_info.scopes.at(m_info.virtualBlocks.at(_functionDefinition).get()).get();
	yulAssert(virtualFunctionScope, "");

	cfg.entry = cfg.makeBlock(debugDataOf(_functionDefinition->body));
	auto arguments = _functionDefinition->parameters | ranges::views::transform([&](auto const& _param) {
		auto const& var = std::get<Scope::Variable>(virtualFunctionScope->identifiers.at(_param.name));
		// Note: cannot use std::make_tuple since it unwraps reference wrappers.
		return std::tuple{std::cref(var), cfg.newVariable(cfg.entry)};
	}) | ranges::to<std::vector>;
	auto returns = _functionDefinition->returnVariables | ranges::views::transform([&](auto const& _param) {
		return std::cref(std::get<Scope::Variable>(virtualFunctionScope->identifiers.at(_param.name)));
	}) | ranges::to<std::vector>;

	cfg.debugData = _functionDefinition->debugData;
	cfg.function = _function;
	cfg.canContinue = m_sideEffects.functionSideEffects().at(_functionDefinition).canContinue;
	cfg.arguments = arguments;
	cfg.returns = returns;

	SSACFGBuilder builder(m_controlFlow, cfg, m_info, m_sideEffects, m_dialect, m_keepLiteralAssignments, m_includeDebugData);
	builder.m_currentBlock = cfg.entry;
	builder.m_functionDefinitions = m_functionDefinitions;
	for (auto&& [var, varId]: cfg.arguments)
		builder.currentDef(var, cfg.entry) = varId;
	for (auto const& var: cfg.returns)
		builder.currentDef(var.get(), cfg.entry) = builder.zero();
	builder.sealBlock(cfg.entry);
	builder(_functionDefinition->body);
	cfg.exits.insert(builder.m_currentBlock);
	// Artificial explicit function exit (`leave`) at the end of the body.
	builder(Leave{debugDataOf(*_functionDefinition)});
	builder.cleanUnreachable();
}

void SSACFGBuilder::operator()(ExpressionStatement const& _expressionStatement)
{
	auto const* functionCall = std::get_if<FunctionCall>(&_expressionStatement.expression);
	yulAssert(functionCall);
	auto results = visitFunctionCall(*functionCall);
	yulAssert(results.empty());
}

void SSACFGBuilder::operator()(Assignment const& _assignment)
{
	assign(
		_assignment.variableNames | ranges::views::transform([&](auto& _var) { return std::ref(lookupVariable(_var.name)); }) | ranges::to<std::vector>,
		_assignment.value.get()
	);
}

void SSACFGBuilder::operator()(VariableDeclaration const& _variableDeclaration)
{
	assign(
		_variableDeclaration.variables | ranges::views::transform([&](auto& _var) { return std::ref(lookupVariable(_var.name)); }) | ranges::to<std::vector>,
		_variableDeclaration.value.get()
	);
}

void SSACFGBuilder::operator()(FunctionDefinition const& _functionDefinition)
{
	Scope::Function const& function = lookupFunction(_functionDefinition.name);
	buildFunctionGraph(&function, &_functionDefinition);
}

void SSACFGBuilder::operator()(If const& _if)
{
	std::optional<bool> constantCondition;
	if (auto const* literalCondition = std::get_if<Literal>(_if.condition.get()))
		constantCondition = literalCondition->value.value() != 0;
	// deal with literal (constant) conditions explicitly
	if (constantCondition)
	{
		if (*constantCondition)
			// Always true - skip conditional, just execute if branch
			(*this)(_if.body);
	}
	else
	{
		auto condition = std::visit(*this, *_if.condition);
		auto ifBranch = m_graph.makeBlock(debugDataOf(_if.body));
		auto afterIf = m_graph.makeBlock(debugDataOf(currentBlock()));
		conditionalJump(
			debugDataOf(_if),
			condition,
			ifBranch,
			afterIf
		);
		sealBlock(ifBranch);
		m_currentBlock = ifBranch;
		(*this)(_if.body);
		jump(debugDataOf(_if.body), afterIf);
		sealBlock(afterIf);
	}
}

void SSACFGBuilder::operator()(Switch const& _switch)
{
	auto expression = std::visit(*this, *_switch.expression);

	auto useJumpTableForSwitch = [](Switch const&) {
		// TODO: check for EOF support & tight switch values.
		return false;
	};
	if (useJumpTableForSwitch(_switch))
	{
		// TODO: also generate a subtraction to shift tight, but non-zero switch cases - or, alternative,
		// transform to zero-based tight switches on Yul if possible.
		std::map<u256, SSACFG::BlockId> cases;
		std::optional<SSACFG::BlockId> defaultCase;
		std::vector<std::tuple<SSACFG::BlockId, std::reference_wrapper<Block const>>> children;
		for (auto const& _case: _switch.cases)
		{
			auto blockId = m_graph.makeBlock(debugDataOf(_case.body));
			if (_case.value)
				cases[_case.value->value.value()] = blockId;
			else
				defaultCase = blockId;
			children.emplace_back(blockId, std::ref(_case.body));
		}
		auto afterSwitch = m_graph.makeBlock(debugDataOf(currentBlock()));

		tableJump(debugDataOf(_switch), expression, cases, defaultCase ? *defaultCase : afterSwitch);
		for (auto [blockId, block]: children)
		{
			sealBlock(blockId);
			m_currentBlock = blockId;
			(*this)(block);
			jump(debugDataOf(currentBlock()), afterSwitch);
		}
		sealBlock(afterSwitch);
		m_currentBlock = afterSwitch;
	}
	else
	{
		if (auto const* constantExpression = std::get_if<Literal>(_switch.expression.get()))
		{
			Case const* matchedCase = nullptr;
			// select case that matches (or default if available)
			for (auto const& switchCase: _switch.cases)
			{
				if (!switchCase.value)
					matchedCase = &switchCase;
				if (switchCase.value && switchCase.value->value.value() == constantExpression->value.value())
				{
					matchedCase = &switchCase;
					break;
				}
			}
			if (matchedCase)
			{
				// inject directly into the current block
				(*this)(matchedCase->body);
			}
			return;
		}

		std::optional<BuiltinHandle> equalityBuiltinHandle = m_dialect.equalityFunctionHandle();
		yulAssert(equalityBuiltinHandle);

		auto makeValueCompare = [&](Case const& _case) {
			FunctionCall const& ghostCall = m_graph.ghostCalls.emplace_back(FunctionCall{
				debugDataOf(_case),
				BuiltinName{{}, *equalityBuiltinHandle},
				{*_case.value /* skip second argument */ }
			});
			auto outputValue = m_graph.newVariable(m_currentBlock);
			currentBlock().operations.emplace_back(SSACFG::Operation{
				{outputValue},
				SSACFG::BuiltinCall{
					debugDataOf(_case),
					m_dialect.builtin(*equalityBuiltinHandle),
					ghostCall
				},
				{m_graph.newLiteral(debugDataOf(_case), _case.value->value.value()), expression}
			});
			return outputValue;
		};

		auto afterSwitch = m_graph.makeBlock(debugDataOf(currentBlock()));
		yulAssert(!_switch.cases.empty(), "");
		for (auto const& switchCase: _switch.cases | ranges::views::drop_last(1))
		{
			yulAssert(switchCase.value, "");
			auto caseBranch = m_graph.makeBlock(debugDataOf(switchCase.body));
			auto elseBranch = m_graph.makeBlock(debugDataOf(_switch));

			conditionalJump(debugDataOf(switchCase), makeValueCompare(switchCase), caseBranch, elseBranch);
			sealBlock(caseBranch);
			sealBlock(elseBranch);
			m_currentBlock = caseBranch;
			(*this)(switchCase.body);
			jump(debugDataOf(switchCase.body), afterSwitch);
			m_currentBlock = elseBranch;
		}
		Case const& switchCase = _switch.cases.back();
		if (switchCase.value)
		{
			auto caseBranch = m_graph.makeBlock(debugDataOf(switchCase.body));
			conditionalJump(debugDataOf(switchCase), makeValueCompare(switchCase), caseBranch, afterSwitch);
			sealBlock(caseBranch);
			m_currentBlock = caseBranch;
		}
		(*this)(switchCase.body);
		jump(debugDataOf(switchCase.body), afterSwitch);
		sealBlock(afterSwitch);
	}
}
void SSACFGBuilder::operator()(ForLoop const& _loop)
{
	ScopedSaveAndRestore scopeRestore(m_scope, m_info.scopes.at(&_loop.pre).get());
	(*this)(_loop.pre);
	auto preLoopDebugData = debugDataOf(currentBlock());

	std::optional<bool> constantCondition;
	if (auto const* literalCondition = std::get_if<Literal>(_loop.condition.get()))
		constantCondition = literalCondition->value.value() != 0;

	SSACFG::BlockId loopCondition = m_graph.makeBlock(debugDataOf(*_loop.condition));
	SSACFG::BlockId loopBody = m_graph.makeBlock(debugDataOf(_loop.body));
	SSACFG::BlockId post = m_graph.makeBlock(debugDataOf(_loop.post));
	SSACFG::BlockId afterLoop = m_graph.makeBlock(preLoopDebugData);

	class ForLoopInfoScope {
	public:
		ForLoopInfoScope(std::stack<ForLoopInfo>& _info, SSACFG::BlockId _breakBlock, SSACFG::BlockId _continueBlock): m_info(_info)
		{
			m_info.push(ForLoopInfo{_breakBlock, _continueBlock});
		}
		~ForLoopInfoScope() {
			m_info.pop();
		}
	private:
		std::stack<ForLoopInfo>& m_info;
	} forLoopInfoScope(m_forLoopInfo, afterLoop, post);

	if (constantCondition.has_value())
	{
		std::visit(*this, *_loop.condition);
		if (*constantCondition)
		{
			jump(debugDataOf(*_loop.condition), loopBody);
			(*this)(_loop.body);
			jump(debugDataOf(_loop.body), post);
			sealBlock(post);
			(*this)(_loop.post);
			jump(debugDataOf(_loop.post), loopBody);
			sealBlock(loopBody);
		}
		else
			jump(debugDataOf(*_loop.condition), afterLoop);
	}
	else
	{
		jump(debugDataOf(_loop.pre), loopCondition);
		auto condition = std::visit(*this, *_loop.condition);
		conditionalJump(debugDataOf(*_loop.condition), condition, loopBody, afterLoop);
		sealBlock(loopBody);
		m_currentBlock = loopBody;
		(*this)(_loop.body);
		jump(debugDataOf(_loop.body), post);
		sealBlock(post);
		(*this)(_loop.post);
		jump(debugDataOf(_loop.post), loopCondition);
		sealBlock(loopCondition);
	}

	sealBlock(afterLoop);
	m_currentBlock = afterLoop;
}

void SSACFGBuilder::operator()(Break const& _break)
{
	yulAssert(!m_forLoopInfo.empty());
	auto currentBlockDebugData = debugDataOf(currentBlock());
	jump(debugDataOf(_break), m_forLoopInfo.top().breakBlock);
	m_currentBlock = m_graph.makeBlock(currentBlockDebugData);
	sealBlock(m_currentBlock);
}

void SSACFGBuilder::operator()(Continue const& _continue)
{
	yulAssert(!m_forLoopInfo.empty());
	auto currentBlockDebugData = debugDataOf(currentBlock());
	jump(debugDataOf(_continue), m_forLoopInfo.top().continueBlock);
	m_currentBlock = m_graph.makeBlock(currentBlockDebugData);
	sealBlock(m_currentBlock);
}

void SSACFGBuilder::operator()(Leave const& _leaveStatement)
{
	auto currentBlockDebugData = debugDataOf(currentBlock());
	currentBlock().exit = SSACFG::BasicBlock::FunctionReturn{
		debugDataOf(_leaveStatement),
		m_graph.returns | ranges::views::transform([&](auto _var) {
			return readVariable(_var, m_currentBlock);
		}) | ranges::to<std::vector>
	};
	m_currentBlock = m_graph.makeBlock(currentBlockDebugData);
	sealBlock(m_currentBlock);
}

void SSACFGBuilder::registerFunctionDefinition(FunctionDefinition const& _functionDefinition)
{
	yulAssert(m_scope, "");
	yulAssert(m_scope->identifiers.count(_functionDefinition.name), "");
	auto& function = std::get<Scope::Function>(m_scope->identifiers.at(_functionDefinition.name));
	m_graph.functions.emplace_back(function);
	m_functionDefinitions.emplace_back(&function, &_functionDefinition);
}

void SSACFGBuilder::operator()(Block const& _block)
{
	ScopedSaveAndRestore saveScope(m_scope, m_info.scopes.at(&_block).get());
	// gather all function definitions so that they are visible to each other's subgraphs
	static constexpr auto functionDefinitionFilter = ranges::views::filter(
		[](auto const& _statement) { return std::holds_alternative<FunctionDefinition>(_statement); }
	);
	for (auto const& statement: _block.statements | functionDefinitionFilter)
		registerFunctionDefinition(std::get<FunctionDefinition>(statement));
	// now visit the rest
	for (auto const& statement: _block.statements)
		std::visit(*this, statement);
}

SSACFG::ValueId SSACFGBuilder::operator()(FunctionCall const& _call)
{
	auto results = visitFunctionCall(_call);
	yulAssert(results.size() == 1);
	return results.front();
}

SSACFG::ValueId SSACFGBuilder::operator()(Identifier const& _identifier)
{
	auto const& var = lookupVariable(_identifier.name);
	return readVariable(var, m_currentBlock);
}

SSACFG::ValueId SSACFGBuilder::operator()(Literal const& _literal)
{
	return m_graph.newLiteral(debugDataOf(currentBlock()), _literal.value.value());
}

void SSACFGBuilder::assign(std::vector<std::reference_wrapper<Scope::Variable const>> _variables, Expression const* _expression)
{
	auto rhs = [&]() -> std::vector<SSACFG::ValueId> {
		if (auto const* functionCall = std::get_if<FunctionCall>(_expression))
			return visitFunctionCall(*functionCall);
		if (_expression)
			return {std::visit(*this, *_expression)};
		return {_variables.size(), zero()};
	}();
	yulAssert(rhs.size() == _variables.size());

	for (auto const& [var, value]: ranges::zip_view(_variables, rhs))
	{
		if (m_keepLiteralAssignments && value.isLiteral())
		{
			SSACFG::Operation assignment{
				.outputs = {m_graph.newVariable(m_currentBlock)},
				.kind = SSACFG::LiteralAssignment{},
				.inputs = {value}
			};
			currentBlock().operations.emplace_back(assignment);
			writeVariable(var, m_currentBlock, assignment.outputs.back());
		}
		else
			writeVariable(var, m_currentBlock, value);
	}

}

std::vector<SSACFG::ValueId> SSACFGBuilder::visitFunctionCall(FunctionCall const& _call)
{
	bool canContinue = true;
	SSACFG::Operation operation = std::visit(util::GenericVisitor{
		[&](BuiltinName const& _builtinName)
		{
			auto const& builtin = m_dialect.builtin(_builtinName.handle);
			SSACFG::Operation result{{}, SSACFG::BuiltinCall{_call.debugData, builtin, _call}, {}};
			result.inputs.reserve(_call.arguments.size());
			result.outputs.reserve(builtin.numReturns);
			for (auto&& [idx, arg]: _call.arguments | ranges::views::enumerate | ranges::views::reverse)
				if (!builtin.literalArgument(idx).has_value())
					result.inputs.emplace_back(std::visit(*this, arg));
			for (size_t i = 0; i < builtin.numReturns; ++i)
				result.outputs.emplace_back(m_graph.newVariable(m_currentBlock));
			canContinue = builtin.controlFlowSideEffects.canContinue;
			return result;
		},
		[&](Identifier const& _identifier)
		{
			YulName const& functionName = _identifier.name;
			Scope::Function const& function = lookupFunction(functionName);
			auto const* definition = findFunctionDefinition(&function);
			yulAssert(definition);
			canContinue = m_sideEffects.functionSideEffects().at(definition).canContinue;
			SSACFG::Operation result{{}, SSACFG::Call{debugDataOf(_call), function, _call, canContinue}, {}};
			result.inputs.reserve(_call.arguments.size());
			result.outputs.reserve(function.numReturns);
			for (auto const& arg: _call.arguments | ranges::views::reverse)
				result.inputs.emplace_back(std::visit(*this, arg));
			for (size_t i = 0; i < function.numReturns; ++i)
				result.outputs.emplace_back(m_graph.newVariable(m_currentBlock));
			return result;
		}
	}, _call.functionName);
	auto results = operation.outputs;
	currentBlock().operations.emplace_back(std::move(operation));
	if (!canContinue)
	{
		currentBlock().exit = SSACFG::BasicBlock::Terminated{};
		m_currentBlock = m_graph.makeBlock(debugDataOf(currentBlock()));
		sealBlock(m_currentBlock);
	}
	return results;
}

SSACFG::ValueId SSACFGBuilder::zero()
{
	return m_graph.newLiteral(debugDataOf(currentBlock()), 0u);
}

SSACFG::ValueId SSACFGBuilder::readVariable(Scope::Variable const& _variable, SSACFG::BlockId _block)
{
	auto const& def = currentDef(_variable, _block);
	if (def.hasValue())
		return def;
	return readVariableRecursive(_variable, _block);
}

SSACFG::ValueId SSACFGBuilder::readVariableRecursive(Scope::Variable const& _variable, SSACFG::BlockId _block)
{
	auto& block = m_graph.block(_block);
	auto& info = blockInfo(_block);

	SSACFG::ValueId val;
	if (!info.sealed)
	{
		// incomplete block: create a phi and defer upsilon emission until the block is sealed
		val = m_graph.newPhi(_block);
		block.phis.push_back(val);
		info.incompletePhis.emplace_back(val, _variable);
	}
	else if (block.entries.size() == 1)
		// one predecessor: no phi needed
		val = readVariable(_variable, *block.entries.begin());
	else
	{
		// Break potential cycles with an argument-less phi; emit upsilons for all predecessors.
		val = m_graph.newPhi(_block);
		block.phis.push_back(val);
		writeVariable(_variable, _block, val);
		// we call tryRemoveTrivialPhi explicitly opposed to what is presented in Algorithm 2, as our implementation
		// does not call it in addPhiOperands to avoid removing phis in unsealed blocks
		addPhiOperands(_variable, val);
		val = tryRemoveTrivialPhi(val);
	}
	writeVariable(_variable, _block, val);
	return val;
}

void SSACFGBuilder::addPhiOperands(Scope::Variable const& _variable, SSACFG::ValueId _phi)
{
	for (auto const& pred: m_graph.block(m_graph.phiInfo(_phi).block).entries)
	{
		auto const val = readVariable(_variable, pred);
		emitUpsilon(pred, val, _phi);
	}
}

void SSACFGBuilder::emitUpsilon(SSACFG::BlockId _block, SSACFG::ValueId _value, SSACFG::ValueId _phi)
{
	yulAssert(_phi.isPhi());
	++s_stats.upsilonsEmitted;
	m_graph.block(_block).upsilons.emplace_back(SSACFG::Upsilon{_value, _phi});
	auto const phiIdx = _phi.value();
	if (phiIdx >= m_upsilonValuesForPhi.size())
		m_upsilonValuesForPhi.resize(phiIdx + 1);
	m_upsilonValuesForPhi[phiIdx].push_back(_value);
}

void SSACFGBuilder::writeVariable(
	Scope::Variable const& _variable, SSACFG::BlockId _block, SSACFG::ValueId _value)
{
	currentDef(_variable, _block) = _value;
}

Scope::Function const& SSACFGBuilder::lookupFunction(YulName _name) const
{
	Scope::Function const* function = nullptr;
	yulAssert(m_scope->lookup(_name, util::GenericVisitor{
		[](Scope::Variable&) { yulAssert(false, "Expected function name."); },
		[&](Scope::Function& _function) { function = &_function; }
	}), "Function name not found.");
	yulAssert(function, "");
	return *function;
}

Scope::Variable const& SSACFGBuilder::lookupVariable(YulName _name) const
{
	yulAssert(m_scope, "");
	Scope::Variable const* var = nullptr;
	if (m_scope->lookup(_name, util::GenericVisitor{
		[&](Scope::Variable const& _var) { var = &_var; },
		[](Scope::Function const&)
		{
			yulAssert(false, "Function not removed during desugaring.");
		}
	}))
	{
		yulAssert(var);
		return *var;
	};
	yulAssert(false, "External identifier access unimplemented.");
}

void SSACFGBuilder::sealBlock(SSACFG::BlockId _block)
{
	// this method deviates from Algorithm 4 in the reference paper,
	// as it would lead to tryRemoveTrivialPhi being called on unsealed blocks
	auto& info = blockInfo(_block);
	yulAssert(!info.sealed, "Trying to seal already sealed block.");
	// Emit upsilons for all incomplete phis before marking the block as sealed.
	for (auto&& [phi, variable] : info.incompletePhis)
		addPhiOperands(variable, phi);
	info.sealed = true;
	for (auto& [phi, _]: info.incompletePhis)
		phi = tryRemoveTrivialPhi(phi);
}


void SSACFGBuilder::conditionalJump(
	langutil::DebugData::ConstPtr _debugData,
	SSACFG::ValueId _condition,
	SSACFG::BlockId _nonZero,
	SSACFG::BlockId _zero
)
{
	currentBlock().exit = SSACFG::BasicBlock::ConditionalJump{
		std::move(_debugData),
		_condition,
		_nonZero,
		_zero
	};
	m_graph.block(_nonZero).entries.push_back(m_currentBlock);
	m_graph.block(_zero).entries.push_back(m_currentBlock);
	m_currentBlock = {};
}

void SSACFGBuilder::jump(
	langutil::DebugData::ConstPtr _debugData,
	SSACFG::BlockId _target
)
{
	currentBlock().exit = SSACFG::BasicBlock::Jump{std::move(_debugData), _target};
	yulAssert(!blockInfo(_target).sealed);
	m_graph.block(_target).entries.push_back(m_currentBlock);
	m_currentBlock = _target;
}

void SSACFGBuilder::tableJump(
	langutil::DebugData::ConstPtr _debugData,
	SSACFG::ValueId _value,
	std::map<u256, SSACFG::BlockId> _cases,
	SSACFG::BlockId _defaultCase)
{
	for (auto caseBlock: _cases | ranges::views::values)
	{
		yulAssert(!blockInfo(caseBlock).sealed);
		m_graph.block(caseBlock).entries.push_back(m_currentBlock);
	}
	yulAssert(!blockInfo(_defaultCase).sealed);
	m_graph.block(_defaultCase).entries.push_back(m_currentBlock);
	currentBlock().exit = SSACFG::BasicBlock::JumpTable{std::move(_debugData), _value, std::move(_cases), _defaultCase};
	m_currentBlock = {};
}

FunctionDefinition const* SSACFGBuilder::findFunctionDefinition(Scope::Function const* _function) const
{
	auto it = std::find_if(
			m_functionDefinitions.begin(),
			m_functionDefinitions.end(),
			[&_function](auto const& _entry) { return std::get<0>(_entry) == _function; }
		);
	if (it != m_functionDefinitions.end())
		return std::get<1>(*it);
	return nullptr;
}
