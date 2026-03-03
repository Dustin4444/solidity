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

#include <libyul/backends/evm/ssa/SSACFGValidator.h>

#include <libyul/AST.h>
#include <libyul/Exceptions.h>
#include <libyul/Utilities.h>

#include <libsolutil/Visitor.h>

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

SSACFGValidator::SSACFGValidator(
	ControlFlow const& _controlFlow,
	SSACFG const& _graph,
	AsmAnalysisInfo const& _analysisInfo,
	ControlFlowSideEffectsCollector const& _sideEffects,
	Dialect const& _dialect,
	bool _keepLiteralAssignments
):
	m_controlFlow(_controlFlow),
	m_graph(_graph),
	m_info(_analysisInfo),
	m_sideEffects(_sideEffects),
	m_dialect(_dialect),
	m_keepLiteralAssignments(_keepLiteralAssignments)
{
}

void SSACFGValidator::validate(
	ControlFlow const& _controlFlow,
	AsmAnalysisInfo const& _analysisInfo,
	Dialect const& _dialect,
	Block const& _ast,
	bool _keepLiteralAssignments
)
{
	ControlFlowSideEffectsCollector sideEffects(_dialect, _ast);

	yulAssert(!_controlFlow.functionGraphs.empty(), "SSACFGValidator: no function graphs");
	SSACFG const& mainGraph = *_controlFlow.functionGraphs.front();

	SSACFGValidator validator(_controlFlow, mainGraph, _analysisInfo, sideEffects, _dialect, _keepLiteralAssignments);
	validator.m_currentBlock = mainGraph.entry;
	bool continues = validator(_ast);
	if (continues)
	{
		yulAssert(
			std::holds_alternative<SSACFG::BasicBlock::MainExit>(validator.currentBlock().exit),
			"SSACFGValidator: main graph does not end with MainExit"
		);
		yulAssert(
			validator.m_opIndex == validator.currentBlock().operations.size(),
			"SSACFGValidator: unconsumed operations in final block"
		);
	}
	else
	{
		yulAssert(
			std::holds_alternative<SSACFG::BasicBlock::Terminated>(validator.currentBlock().exit),
			"SSACFGValidator: non-continuing main graph does not end with Terminated"
		);
	}
}

// ---------------------------------------------------------------------------
// CFG navigation
// ---------------------------------------------------------------------------

SSACFG::Operation const& SSACFGValidator::nextOperation()
{
	auto const& block = currentBlock();
	yulAssert(
		m_opIndex < block.operations.size(),
		fmt::format("SSACFGValidator: expected operation at block {} index {}, but block only has {} operations",
			m_currentBlock.value, m_opIndex, block.operations.size())
	);
	return block.operations[m_opIndex++];
}

void SSACFGValidator::advanceToBlock(SSACFG::BlockId _block)
{
	m_currentBlock = _block;
	m_opIndex = 0;
}

SSACFG::BasicBlock::ConditionalJump const& SSACFGValidator::expectConditionalJump()
{
	yulAssert(
		m_opIndex == currentBlock().operations.size(),
		"SSACFGValidator: unconsumed operations before conditional jump"
	);
	auto const* condJump = std::get_if<SSACFG::BasicBlock::ConditionalJump>(&currentBlock().exit);
	yulAssert(condJump, "SSACFGValidator: expected ConditionalJump exit");
	return *condJump;
}

SSACFG::BasicBlock::Jump const& SSACFGValidator::expectJump()
{
	yulAssert(
		m_opIndex == currentBlock().operations.size(),
		"SSACFGValidator: unconsumed operations before jump"
	);
	auto const* jump = std::get_if<SSACFG::BasicBlock::Jump>(&currentBlock().exit);
	yulAssert(jump, "SSACFGValidator: expected Jump exit");
	return *jump;
}

// ---------------------------------------------------------------------------
// Phi resolution
// ---------------------------------------------------------------------------

void SSACFGValidator::resolvePhis(
	SSACFG::BlockId _mergeBlock,
	std::vector<std::pair<SSACFG::BlockId, VarState>> const& _branches
)
{
	auto const& mergeBlockData = m_graph.block(_mergeBlock);

	// Only process variables that exist in ALL branch states.
	// Variables that only exist in some branches are inner-scope variables
	// that are no longer live at the merge point.
	VarState mergedState;
	for (auto const& [var, _]: _branches.front().second)
	{
		bool inAll = true;
		for (size_t i = 1; i < _branches.size(); ++i)
			if (_branches[i].second.find(var) == _branches[i].second.end())
			{
				inAll = false;
				break;
			}
		if (!inAll)
			continue;

		// Collect the value of this variable from each branch
		std::vector<SSACFG::ValueId> branchValues;
		bool allSame = true;
		for (auto const& [predBlock, predState]: _branches)
		{
			branchValues.push_back(predState.at(var));
			if (branchValues.back() != branchValues.front())
				allSame = false;
		}

		if (allSame)
		{
			// All branches agree: no phi needed (trivial phi was removed)
			mergedState[var] = branchValues.front();
		}
		else
		{
			// Find a phi in the merge block whose arguments match
			bool found = false;
			for (auto phi: mergeBlockData.phis)
			{
				auto const& phiInfo = m_graph.phiInfo(phi);
				bool matches = true;
				// todo
				/*for (size_t i = 0; i < _branches.size(); ++i)
				{
					auto entryOffset = m_graph.phiArgumentIndex(_branches[i].first, _mergeBlock);
					if (phiInfo.arguments.at(entryOffset) != branchValues[i])
					{
						matches = false;
						break;
					}
				}
				if (matches)
				{
					mergedState[var] = phi;
					found = true;
					break;
				}*/
			}
			yulAssert(found, "SSACFGValidator: no matching phi found for variable at merge point");
		}
	}
	m_variableValues = mergedState;
}

SSACFGValidator::PhiVarMap SSACFGValidator::tentativeResolvePhis(SSACFG::BlockId _source, SSACFG::BlockId _target)
{
	PhiVarMap phiVarMap;
	auto const& targetBlock = m_graph.block(_target);
	if (targetBlock.phis.empty())
		return phiVarMap;

	auto entryOffset = 0; //m_graph.phiArgumentIndex(_source, _target);

	for (auto phi: targetBlock.phis)
	{
		auto const& phiInfo = m_graph.phiInfo(phi);
		/*auto argValue = phiInfo.arguments.at(entryOffset);

		// Find a variable whose current value matches argValue
		Scope::Variable const* matchedVar = nullptr;
		for (auto& [var, val]: m_variableValues)
		{
			if (val == argValue)
			{
				matchedVar = var;
				break;
			}
		}
		if (matchedVar)
		{
			phiVarMap[phi] = matchedVar;
			m_variableValues[matchedVar] = phi;
		}*/
	}
	return phiVarMap;
}

void SSACFGValidator::verifyBackEdgePhis(SSACFG::BlockId _source, SSACFG::BlockId _target, PhiVarMap const& _phiVarMap)
{
	auto const& targetBlock = m_graph.block(_target);
	/*auto entryOffset = m_graph.phiArgumentIndex(_source, _target);

	for (auto phi: targetBlock.phis)
	{
		auto const& phiInfo = m_graph.phiInfo(phi);
		auto backEdgeArg = phiInfo.arguments.at(entryOffset);

		auto it = _phiVarMap.find(phi);
		yulAssert(it != _phiVarMap.end(), "SSACFGValidator: phi not tracked from tentative resolution");
		auto const* var = it->second;
		yulAssert(
			m_variableValues.at(var) == backEdgeArg,
			fmt::format("SSACFGValidator: back edge phi argument mismatch: variable has {}, phi expects {}",
				m_variableValues.at(var), backEdgeArg)
		);
	}*/
}

// ---------------------------------------------------------------------------
// Literal/variable helpers
// ---------------------------------------------------------------------------

SSACFG::ValueId SSACFGValidator::lookupLiteral(u256 const& _value)
{
	auto result = m_graph.lookupLiteral(_value);
	yulAssert(result.has_value(), fmt::format("SSACFGValidator: literal value {} not found in CFG", formatNumber(_value)));
	return *result;
}

Scope::Function const& SSACFGValidator::lookupFunction(YulName _name) const
{
	Scope::Function const* function = nullptr;
	yulAssert(m_scope->lookup(_name, util::GenericVisitor{
		[](Scope::Variable&) { yulAssert(false, "Expected function name."); },
		[&](Scope::Function& _function) { function = &_function; }
	}), "Function name not found.");
	yulAssert(function, "");
	return *function;
}

Scope::Variable const& SSACFGValidator::lookupVariable(YulName _name) const
{
	yulAssert(m_scope, "");
	Scope::Variable const* var = nullptr;
	if (m_scope->lookup(_name, util::GenericVisitor{
		[&](Scope::Variable const& _var) { var = &_var; },
		[](Scope::Function const&) { yulAssert(false, "Function not removed during desugaring."); }
	}))
	{
		yulAssert(var);
		return *var;
	};
	yulAssert(false, "External identifier access unimplemented.");
}

void SSACFGValidator::registerFunctionDefinition(FunctionDefinition const& _functionDefinition)
{
	yulAssert(m_scope, "");
	yulAssert(m_scope->identifiers.count(_functionDefinition.name), "");
	auto& function = std::get<Scope::Function>(m_scope->identifiers.at(_functionDefinition.name));
	m_functionDefinitions.emplace_back(&function, &_functionDefinition);
}

FunctionDefinition const* SSACFGValidator::findFunctionDefinition(Scope::Function const* _function) const
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

// ---------------------------------------------------------------------------
// Statement visitors
// ---------------------------------------------------------------------------

bool SSACFGValidator::operator()(Block const& _block)
{
	ScopedSaveAndRestore saveScope(m_scope, m_info.scopes.at(&_block).get());
	static constexpr auto functionDefinitionFilter = ranges::views::filter(
		[](auto const& _statement) { return std::holds_alternative<FunctionDefinition>(_statement); }
	);
	for (auto const& statement: _block.statements | functionDefinitionFilter)
		registerFunctionDefinition(std::get<FunctionDefinition>(statement));
	for (auto const& statement: _block.statements)
	{
		bool continues = std::visit([this](auto const& _stmt) -> bool { return (*this)(_stmt); }, statement);
		if (!continues)
			return false;
	}
	return true;
}

bool SSACFGValidator::operator()(ExpressionStatement const& _expressionStatement)
{
	auto const* functionCall = std::get_if<FunctionCall>(&_expressionStatement.expression);
	yulAssert(functionCall);
	bool canContinue = true;
	auto results = validateFunctionCall(*functionCall, canContinue);
	yulAssert(results.empty());
	return canContinue;
}

bool SSACFGValidator::operator()(Assignment const& _assignment)
{
	return validateAssign(
		_assignment.variableNames | ranges::views::transform([&](auto& _var) { return std::ref(lookupVariable(_var.name)); }) | ranges::to<std::vector>,
		_assignment.value.get()
	);
}

bool SSACFGValidator::operator()(VariableDeclaration const& _variableDeclaration)
{
	return validateAssign(
		_variableDeclaration.variables | ranges::views::transform([&](auto& _var) { return std::ref(lookupVariable(_var.name)); }) | ranges::to<std::vector>,
		_variableDeclaration.value.get()
	);
}

bool SSACFGValidator::operator()(FunctionDefinition const& _functionDefinition)
{
	Scope::Function const& function = lookupFunction(_functionDefinition.name);

	SSACFG const* funcGraph = m_controlFlow.functionGraph(&function);
	yulAssert(funcGraph, fmt::format("SSACFGValidator: no function graph for '{}'", _functionDefinition.name.str()));

	yulAssert(
		funcGraph->arguments.size() == _functionDefinition.parameters.size(),
		"SSACFGValidator: function argument count mismatch"
	);
	yulAssert(
		funcGraph->returns.size() == _functionDefinition.returnVariables.size(),
		"SSACFGValidator: function return count mismatch"
	);
	yulAssert(funcGraph->function == &function, "SSACFGValidator: function pointer mismatch");

	yulAssert(m_info.scopes.at(&_functionDefinition.body), "");
	Scope* virtualFunctionScope = m_info.scopes.at(m_info.virtualBlocks.at(&_functionDefinition).get()).get();
	yulAssert(virtualFunctionScope, "");

	SSACFGValidator funcValidator(m_controlFlow, *funcGraph, m_info, m_sideEffects, m_dialect, m_keepLiteralAssignments);
	funcValidator.m_functionDefinitions = m_functionDefinitions;
	funcValidator.m_currentBlock = funcGraph->entry;

	// Set up argument variable mappings
	for (auto&& [cfgArg, astParam]: ranges::zip_view(funcGraph->arguments, _functionDefinition.parameters))
	{
		auto const& [varRef, valueId] = cfgArg;
		auto const& expectedVar = std::get<Scope::Variable>(virtualFunctionScope->identifiers.at(astParam.name));
		yulAssert(&varRef.get() == &expectedVar, "SSACFGValidator: function argument variable mismatch");
		funcValidator.m_variableValues[&varRef.get()] = valueId;
	}
	// Set up return variable mappings (initialized to zero)
	for (auto const& varRef: funcGraph->returns)
	{
		auto zeroLit = funcGraph->lookupLiteral(0);
		yulAssert(zeroLit.has_value(), "SSACFGValidator: literal 0 not found in function graph");
		funcValidator.m_variableValues[&varRef.get()] = *zeroLit;
	}

	bool bodyContinues = funcValidator(_functionDefinition.body);
	if (bodyContinues)
	{
		// The builder inserts a Leave at the end
		funcValidator(Leave{debugDataOf(_functionDefinition)});
	}
	return true;
}

bool SSACFGValidator::operator()(If const& _if)
{
	std::optional<bool> constantCondition;
	if (auto const* literalCondition = std::get_if<Literal>(_if.condition.get()))
		constantCondition = literalCondition->value.value() != 0;

	if (constantCondition)
	{
		if (*constantCondition)
			return (*this)(_if.body);
		// else: dead code, nothing in CFG
		return true;
	}

	auto condition = std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_if.condition);
	auto const& condJump = expectConditionalJump();
	yulAssert(
		condJump.condition == condition,
		fmt::format("SSACFGValidator: if condition mismatch: expected {}, got {}", condition, condJump.condition)
	);
	auto ifBranch = condJump.nonZero;
	auto afterIf = condJump.zero;

	auto stateFalse = saveState();
	advanceToBlock(ifBranch);
	bool bodyContinues = (*this)(_if.body);

	if (bodyContinues)
	{
		auto const& jump = expectJump();
		yulAssert(jump.target == afterIf, "SSACFGValidator: if body does not jump to afterIf block");
		auto stateTrue = saveState();
		auto preIfBlock = *m_graph.block(ifBranch).entries.begin();
		resolvePhis(afterIf, {{preIfBlock, stateFalse}, {m_currentBlock, stateTrue}});
	}
	else
	{
		// Body doesn't continue; afterIf only reachable from the false branch
		restoreState(stateFalse);
	}
	advanceToBlock(afterIf);
	return true;
}

bool SSACFGValidator::operator()(Switch const& _switch)
{
	if (auto const* constantExpression = std::get_if<Literal>(_switch.expression.get()))
	{
		auto expression = std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_switch.expression);
		(void)expression;
		Case const* matchedCase = nullptr;
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
			return (*this)(matchedCase->body);
		return true;
	}

	auto expression = std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_switch.expression);

	std::optional<BuiltinHandle> equalityBuiltinHandle = m_dialect.equalityFunctionHandle();
	yulAssert(equalityBuiltinHandle);

	auto validateValueCompare = [&](Case const& _case) {
		auto const& op = nextOperation();
		yulAssert(op.outputs.size() == 1, "SSACFGValidator: switch compare should have 1 output");
		auto const* builtinCall = std::get_if<SSACFG::BuiltinCall>(&op.kind);
		yulAssert(builtinCall, "SSACFGValidator: switch compare should be a BuiltinCall");
		yulAssert(
			builtinCall->builtin.get().name == m_dialect.builtin(*equalityBuiltinHandle).name,
			"SSACFGValidator: switch compare should use equality builtin"
		);
		yulAssert(op.inputs.size() == 2, "SSACFGValidator: switch compare should have 2 inputs");
		yulAssert(op.inputs[0].isLiteral(), "SSACFGValidator: switch compare first input should be literal");
		yulAssert(
			m_graph.literalInfo(op.inputs[0]).value == _case.value->value.value(),
			"SSACFGValidator: switch case literal mismatch"
		);
		yulAssert(op.inputs[1] == expression, "SSACFGValidator: switch compare second input should be the switch expression");
		return op.outputs.front();
	};

	yulAssert(!_switch.cases.empty(), "");

	std::optional<SSACFG::BlockId> afterSwitch;
	std::vector<std::pair<SSACFG::BlockId, VarState>> continuingBranches;
	auto stateBeforeSwitch = saveState();

	for (auto const& switchCase: _switch.cases | ranges::views::drop_last(1))
	{
		yulAssert(switchCase.value, "");
		auto compareResult = validateValueCompare(switchCase);
		auto const& condJump = expectConditionalJump();
		yulAssert(condJump.condition == compareResult, "SSACFGValidator: switch conditional jump condition mismatch");
		auto caseBranch = condJump.nonZero;
		auto elseBranch = condJump.zero;

		auto savedState = saveState();
		advanceToBlock(caseBranch);
		bool bodyContinues = (*this)(switchCase.body);

		if (bodyContinues)
		{
			auto const& jump = expectJump();
			if (!afterSwitch.has_value())
				afterSwitch = jump.target;
			else
				yulAssert(jump.target == *afterSwitch, "SSACFGValidator: switch case does not jump to afterSwitch");
			continuingBranches.emplace_back(m_currentBlock, saveState());
		}

		restoreState(savedState);
		advanceToBlock(elseBranch);
	}

	// Last case
	Case const& lastCase = _switch.cases.back();
	if (lastCase.value)
	{
		auto compareResult = validateValueCompare(lastCase);
		auto const& condJump = expectConditionalJump();
		yulAssert(condJump.condition == compareResult, "SSACFGValidator: last switch case conditional jump condition mismatch");
		auto caseBranch = condJump.nonZero;
		auto afterSwitchFromLast = condJump.zero;

		if (!afterSwitch.has_value())
			afterSwitch = afterSwitchFromLast;
		else
			yulAssert(afterSwitchFromLast == *afterSwitch, "SSACFGValidator: last case afterSwitch mismatch");

		advanceToBlock(caseBranch);
		bool bodyContinues = (*this)(lastCase.body);
		if (bodyContinues)
		{
			auto const& jump = expectJump();
			yulAssert(jump.target == *afterSwitch, "SSACFGValidator: last case does not jump to afterSwitch");
			continuingBranches.emplace_back(m_currentBlock, saveState());
		}
	}
	else
	{
		// Default case (no comparison)
		bool bodyContinues = (*this)(lastCase.body);
		if (bodyContinues)
		{
			auto const& jump = expectJump();
			if (!afterSwitch.has_value())
				afterSwitch = jump.target;
			else
				yulAssert(jump.target == *afterSwitch, "SSACFGValidator: default case does not jump to afterSwitch");
			continuingBranches.emplace_back(m_currentBlock, saveState());
		}
	}

	if (afterSwitch.has_value() && !continuingBranches.empty())
	{
		if (continuingBranches.size() == 1)
			restoreState(continuingBranches.front().second);
		else
			resolvePhis(*afterSwitch, continuingBranches);
		advanceToBlock(*afterSwitch);
	}
	return afterSwitch.has_value() && !continuingBranches.empty();
}

bool SSACFGValidator::operator()(ForLoop const& _loop)
{
	ScopedSaveAndRestore scopeRestore(m_scope, m_info.scopes.at(&_loop.pre).get());
	(*this)(_loop.pre);

	std::optional<bool> constantCondition;
	if (auto const* literalCondition = std::get_if<Literal>(_loop.condition.get()))
		constantCondition = literalCondition->value.value() != 0;

	if (constantCondition.has_value())
	{
		std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_loop.condition);
		if (*constantCondition)
		{
			// Infinite loop (constant true)
			auto const& jumpToBody = expectJump();
			auto loopBody = jumpToBody.target;
			// Builder creates: loopCondition, loopBody, post, afterLoop
			// For constant true, it jumps directly to loopBody.
			// Block IDs: loopCondition = loopBody - 1, post = loopBody + 1, afterLoop = loopBody + 2
			// Actually the builder allocates all four before checking the condition:
			// loopCondition, loopBody, post, afterLoop in sequence.
			// So: post = loopCondition + 2 = loopBody + 1, afterLoop = loopCondition + 3 = loopBody + 2
			SSACFG::BlockId post{loopBody.value + 1};
			SSACFG::BlockId afterLoop{loopBody.value + 2};

			auto preBlock = m_currentBlock;
			advanceToBlock(loopBody);
			auto phiVarMap = tentativeResolvePhis(preBlock, loopBody);

			{
				class ForLoopInfoScope {
				public:
					ForLoopInfoScope(std::stack<ForLoopInfo>& _info, SSACFG::BlockId _breakBlock, SSACFG::BlockId _continueBlock): m_info(_info) {
						m_info.push(ForLoopInfo{_breakBlock, _continueBlock});
					}
					~ForLoopInfoScope() { m_info.pop(); }
				private:
					std::stack<ForLoopInfo>& m_info;
				} forLoopScope(m_forLoopInfo, afterLoop, post);

				bool bodyContinues = (*this)(_loop.body);

				if (bodyContinues)
				{
					auto const& jumpToPost = expectJump();
					yulAssert(jumpToPost.target == post, "SSACFGValidator: for body does not jump to post block");
					advanceToBlock(post);
					(*this)(_loop.post);
					auto const& backEdge = expectJump();
					yulAssert(backEdge.target == loopBody, "SSACFGValidator: for post does not jump back to loop body");
					verifyBackEdgePhis(m_currentBlock, loopBody, phiVarMap);
				}
			}

			// afterLoop is only reachable via break
			// Check if afterLoop block has any entries
			auto const& afterLoopBlock = m_graph.block(afterLoop);
			if (!afterLoopBlock.entries.empty())
			{
				advanceToBlock(afterLoop);
				return true;
			}
			return false;
		}
		else
		{
			// Never-executing loop (constant false)
			auto const& jumpToAfter = expectJump();
			advanceToBlock(jumpToAfter.target);
			return true;
		}
	}
	else
	{
		// Dynamic condition
		auto preBlock = m_currentBlock;
		auto const& jumpToCond = expectJump();
		auto loopCondition = jumpToCond.target;

		// Builder creates: loopCondition, loopBody, post, afterLoop consecutively
		SSACFG::BlockId loopBody{loopCondition.value + 1};
		SSACFG::BlockId post{loopCondition.value + 2};
		SSACFG::BlockId afterLoop{loopCondition.value + 3};

		advanceToBlock(loopCondition);
		auto phiVarMap = tentativeResolvePhis(preBlock, loopCondition);

		auto condition = std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_loop.condition);
		auto const& condJump = expectConditionalJump();
		yulAssert(condJump.condition == condition, "SSACFGValidator: for loop condition mismatch");
		yulAssert(condJump.nonZero == loopBody, "SSACFGValidator: for loop condition nonZero mismatch");
		yulAssert(condJump.zero == afterLoop, "SSACFGValidator: for loop condition zero mismatch");

		auto stateAtCondition = saveState();

		advanceToBlock(loopBody);

		{
			class ForLoopInfoScope {
			public:
				ForLoopInfoScope(std::stack<ForLoopInfo>& _info, SSACFG::BlockId _breakBlock, SSACFG::BlockId _continueBlock): m_info(_info) {
					m_info.push(ForLoopInfo{_breakBlock, _continueBlock});
				}
				~ForLoopInfoScope() { m_info.pop(); }
			private:
				std::stack<ForLoopInfo>& m_info;
			} forLoopScope(m_forLoopInfo, afterLoop, post);

			bool bodyContinues = (*this)(_loop.body);

			if (bodyContinues)
			{
				auto const& jumpToPost = expectJump();
				yulAssert(jumpToPost.target == post, "SSACFGValidator: for body does not jump to post block");
				advanceToBlock(post);
				(*this)(_loop.post);
				auto const& backEdge = expectJump();
				yulAssert(backEdge.target == loopCondition, "SSACFGValidator: for post does not jump back to loop condition");
				verifyBackEdgePhis(m_currentBlock, loopCondition, phiVarMap);
			}
		}

		// afterLoop receives state from loopCondition (zero branch)
		restoreState(stateAtCondition);
		advanceToBlock(afterLoop);
		return true;
	}
}

bool SSACFGValidator::operator()(Break const&)
{
	yulAssert(!m_forLoopInfo.empty());
	auto const& jump = expectJump();
	yulAssert(jump.target == m_forLoopInfo.top().breakBlock, "SSACFGValidator: break does not jump to break block");
	return false;
}

bool SSACFGValidator::operator()(Continue const&)
{
	yulAssert(!m_forLoopInfo.empty());
	auto const& jump = expectJump();
	yulAssert(jump.target == m_forLoopInfo.top().continueBlock, "SSACFGValidator: continue does not jump to continue block");
	return false;
}

bool SSACFGValidator::operator()(Leave const&)
{
	yulAssert(
		m_opIndex == currentBlock().operations.size(),
		"SSACFGValidator: unconsumed operations before leave"
	);
	auto const* funcReturn = std::get_if<SSACFG::BasicBlock::FunctionReturn>(&currentBlock().exit);
	yulAssert(funcReturn, "SSACFGValidator: expected FunctionReturn exit for leave");

	yulAssert(
		funcReturn->returnValues.size() == m_graph.returns.size(),
		"SSACFGValidator: return value count mismatch"
	);
	for (size_t i = 0; i < m_graph.returns.size(); ++i)
	{
		auto expected = m_variableValues.at(&m_graph.returns[i].get());
		yulAssert(
			funcReturn->returnValues[i] == expected,
			fmt::format("SSACFGValidator: return value {} mismatch: expected {}, got {}",
				i, expected, funcReturn->returnValues[i])
		);
	}
	return false;
}

// ---------------------------------------------------------------------------
// Expression visitors
// ---------------------------------------------------------------------------

SSACFG::ValueId SSACFGValidator::operator()(FunctionCall const& _call)
{
	bool canContinue = true;
	auto results = validateFunctionCall(_call, canContinue);
	yulAssert(results.size() == 1);
	return results.front();
}

SSACFG::ValueId SSACFGValidator::operator()(Identifier const& _identifier)
{
	auto const& var = lookupVariable(_identifier.name);
	return m_variableValues.at(&var);
}

SSACFG::ValueId SSACFGValidator::operator()(Literal const& _literal)
{
	return lookupLiteral(_literal.value.value());
}

// ---------------------------------------------------------------------------
// Assignment/call helpers
// ---------------------------------------------------------------------------

bool SSACFGValidator::validateAssign(
	std::vector<std::reference_wrapper<Scope::Variable const>> _variables,
	Expression const* _expression
)
{
	bool canContinue = true;
	auto rhs = [&]() -> std::vector<SSACFG::ValueId> {
		if (auto const* functionCall = std::get_if<FunctionCall>(_expression))
			return validateFunctionCall(*functionCall, canContinue);
		if (_expression)
			return {std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, *_expression)};
		return {_variables.size(), lookupLiteral(0)};
	}();
	yulAssert(rhs.size() == _variables.size());

	for (auto const& [var, value]: ranges::zip_view(_variables, rhs))
	{
		if (m_keepLiteralAssignments && value.isLiteral())
		{
			auto const& op = nextOperation();
			yulAssert(op.outputs.size() == 1, "SSACFGValidator: LiteralAssignment should have 1 output");
			auto const* litAssign = std::get_if<SSACFG::LiteralAssignment>(&op.kind);
			yulAssert(litAssign, "SSACFGValidator: expected LiteralAssignment operation");
			yulAssert(op.inputs.size() == 1, "SSACFGValidator: LiteralAssignment should have 1 input");
			yulAssert(op.inputs[0] == value, "SSACFGValidator: LiteralAssignment input mismatch");
			m_variableValues[&var.get()] = op.outputs.back();
		}
		else
			m_variableValues[&var.get()] = value;
	}
	return canContinue;
}

std::vector<SSACFG::ValueId> SSACFGValidator::validateFunctionCall(FunctionCall const& _call, bool& _canContinue)
{
	// Arguments are evaluated BEFORE the call operation is appended (matching builder order).
	// Sub-expressions in arguments may generate their own operations.
	_canContinue = true;
	/*std::vector<SSACFG::ValueId> expectedInputs;

	std::visit(util::GenericVisitor{
		[&](BuiltinName const& _builtinName)
		{
			auto const& builtin = m_dialect.builtin(_builtinName.handle);
			for (auto&& [idx, arg]: _call.arguments | ranges::views::enumerate | ranges::views::reverse)
				if (!builtin.literalArgument(idx).has_value())
					expectedInputs.emplace_back(std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, arg));

			auto const& op = nextOperation();
			auto const* builtinCall = std::get_if<SSACFG::BuiltinCall>(&op.kind);
			yulAssert(builtinCall, "SSACFGValidator: expected BuiltinCall operation");
			yulAssert(
				&builtinCall->builtin.get() == &builtin,
				fmt::format("SSACFGValidator: builtin mismatch: expected '{}', got '{}'", builtin.name, builtinCall->builtin.get().name)
			);
			yulAssert(&builtinCall->call.get() == &_call, "SSACFGValidator: FunctionCall AST reference mismatch");
			yulAssert(
				op.inputs == expectedInputs,
				[&]() {
					std::string msg = fmt::format(
						"SSACFGValidator: builtin call input values mismatch for '{}' in block {} op {}\n  actual inputs:   [",
						builtin.name, m_currentBlock.value, m_opIndex - 1
					);
					for (size_t i = 0; i < op.inputs.size(); ++i)
						msg += fmt::format("{}{}", i ? ", " : "", op.inputs[i]);
					msg += "]\n  expected inputs: [";
					for (size_t i = 0; i < expectedInputs.size(); ++i)
						msg += fmt::format("{}{}", i ? ", " : "", expectedInputs[i]);
					msg += fmt::format("]\n  arg count: {}", _call.arguments.size());
					return msg;
				}()
			);
			yulAssert(
				op.outputs.size() == builtin.numReturns,
				"SSACFGValidator: builtin call output count mismatch"
			);
			_canContinue = builtin.controlFlowSideEffects.canContinue;
		},
		[&](Identifier const& _identifier)
		{
			Scope::Function const& function = lookupFunction(_identifier.name);
			for (auto const& arg: _call.arguments | ranges::views::reverse)
				expectedInputs.emplace_back(std::visit([this](auto const& _e) -> SSACFG::ValueId { return (*this)(_e); }, arg));

			auto const& op = nextOperation();
			auto const* call = std::get_if<SSACFG::Call>(&op.kind);
			yulAssert(call, "SSACFGValidator: expected Call operation");
			yulAssert(
				&call->function.get() == &function,
				"SSACFGValidator: function call target mismatch"
			);
			yulAssert(&call->call.get() == &_call, "SSACFGValidator: FunctionCall AST reference mismatch");

			auto const* definition = findFunctionDefinition(&function);
			yulAssert(definition);
			_canContinue = m_sideEffects.functionSideEffects().at(definition).canContinue;
			yulAssert(
				op.inputs == expectedInputs,
				"SSACFGValidator: user function call input values mismatch"
			);
			yulAssert(
				op.outputs.size() == function.numReturns,
				"SSACFGValidator: user function call output count mismatch"
			);
		}
	}, _call.functionName);

	// The operation was consumed inside the lambda; re-read it for outputs
	auto const& op = currentBlock().operations[m_opIndex - 1];
	auto results = op.outputs;
	if (!_canContinue)
	{
		yulAssert(
			std::holds_alternative<SSACFG::BasicBlock::Terminated>(currentBlock().exit),
			"SSACFGValidator: non-continuing call should terminate block"
		);
	}*/
	return {}; //results;
}
