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

/// Validates that an SSA CFG faithfully represents its originating Yul AST.
///
/// Walks the AST and CFG in lockstep, maintaining a cursor into the CFG
/// (current block + operation index) and a variable-to-ValueId mapping,
/// asserting at every step that the CFG's operations, control flow, and
/// values correspond to the AST.

#pragma once

#include <libyul/backends/evm/ssa/ControlFlow.h>
#include <libyul/ControlFlowSideEffectsCollector.h>

#include <map>
#include <stack>
#include <vector>

namespace solidity::yul::ssa
{

class SSACFGValidator
{
public:
	/// Validate that a ControlFlow is a faithful representation of the AST block.
	/// yulAsserts on any inconsistency.
	static void validate(
		ControlFlow const& _controlFlow,
		AsmAnalysisInfo const& _analysisInfo,
		Dialect const& _dialect,
		Block const& _ast,
		bool _keepLiteralAssignments
	);

private:
	SSACFGValidator(
		ControlFlow const& _controlFlow,
		SSACFG const& _graph,
		AsmAnalysisInfo const& _analysisInfo,
		ControlFlowSideEffectsCollector const& _sideEffects,
		Dialect const& _dialect,
		bool _keepLiteralAssignments
	);

	// Statement visitors — return false if control flow terminates
	bool operator()(ExpressionStatement const& _statement);
	bool operator()(Assignment const& _assignment);
	bool operator()(VariableDeclaration const& _varDecl);
	bool operator()(FunctionDefinition const& _functionDefinition);
	bool operator()(If const& _if);
	bool operator()(Switch const& _switch);
	bool operator()(ForLoop const& _loop);
	bool operator()(Break const& _break);
	bool operator()(Continue const& _continue);
	bool operator()(Leave const& _leave);
	bool operator()(Block const& _block);

	// Expression visitors — return the ValueId that the CFG assigned
	SSACFG::ValueId operator()(FunctionCall const& _call);
	SSACFG::ValueId operator()(Identifier const& _identifier);
	SSACFG::ValueId operator()(Literal const& _literal);

	// Assignment/call helpers
	bool validateAssign(std::vector<std::reference_wrapper<Scope::Variable const>> _variables, Expression const* _expression);
	std::vector<SSACFG::ValueId> validateFunctionCall(FunctionCall const& _call, bool& _canContinue);

	// CFG navigation
	SSACFG::Operation const& nextOperation();
	SSACFG::BasicBlock const& currentBlock() const { return m_graph.block(m_currentBlock); }
	void advanceToBlock(SSACFG::BlockId _block);

	// Control flow checks
	SSACFG::BasicBlock::ConditionalJump const& expectConditionalJump();
	SSACFG::BasicBlock::Jump const& expectJump();

	// Phi resolution
	using VarState = std::map<Scope::Variable const*, SSACFG::ValueId>;
	VarState saveState() const { return m_variableValues; }
	void restoreState(VarState const& _state) { m_variableValues = _state; }
	void resolvePhis(
		SSACFG::BlockId _mergeBlock,
		std::vector<std::pair<SSACFG::BlockId, VarState>> const& _branches
	);
	using PhiVarMap = std::map<SSACFG::ValueId, Scope::Variable const*>;
	PhiVarMap tentativeResolvePhis(SSACFG::BlockId _source, SSACFG::BlockId _target);
	void verifyBackEdgePhis(SSACFG::BlockId _source, SSACFG::BlockId _target, PhiVarMap const& _phiVarMap);

	// Literal/variable helpers
	SSACFG::ValueId lookupLiteral(u256 const& _value);
	Scope::Variable const& lookupVariable(YulName _name) const;
	Scope::Function const& lookupFunction(YulName _name) const;
	void registerFunctionDefinition(FunctionDefinition const& _functionDefinition);
	FunctionDefinition const* findFunctionDefinition(Scope::Function const* _function) const;

	ControlFlow const& m_controlFlow;
	SSACFG const& m_graph;
	AsmAnalysisInfo const& m_info;
	ControlFlowSideEffectsCollector const& m_sideEffects;
	Dialect const& m_dialect;
	bool const m_keepLiteralAssignments;

	SSACFG::BlockId m_currentBlock;
	size_t m_opIndex = 0;
	Scope* m_scope = nullptr;
	VarState m_variableValues;
	std::vector<std::tuple<Scope::Function const*, FunctionDefinition const*>> m_functionDefinitions;

	struct ForLoopInfo {
		SSACFG::BlockId breakBlock;
		SSACFG::BlockId continueBlock;
	};
	std::stack<ForLoopInfo> m_forLoopInfo;
};

}
