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

#include <test/tools/ossfuzz/sol2Proto.pb.h>

#include <random>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>

namespace solidity::test::sol2protofuzzer
{

/// Random number generator seeded by fuzzer-supplied seed.
struct SolRandomNumGenerator
{
	using RandomEngine = std::minstd_rand;

	explicit SolRandomNumGenerator(unsigned _seed): m_random(RandomEngine(_seed)) {}

	unsigned operator()()
	{
		return static_cast<unsigned>(m_random());
	}

	RandomEngine m_random;
};

class ProtoConverter
{
public:
	ProtoConverter() = default;
	ProtoConverter(ProtoConverter const&) = delete;
	ProtoConverter(ProtoConverter&&) = delete;

	/// Convert a protobuf Program to Solidity source code.
	std::string protoToSolidity(Program const& _p);

private:
	// ===== Internal info types =====

	struct FuncInfo
	{
		std::string name;
		unsigned numParams;
		Visibility vis;
		StateMutability mut;
	};

	struct EventInfo
	{
		std::string name;
		unsigned numParams;
	};

	struct ErrorInfo
	{
		std::string name;
		unsigned numParams;
	};

	struct ContractInfo
	{
		std::string name;
		ContractDef::Kind kind;
		std::vector<FuncInfo> functions;
		/// State variable declarations: (name, type-string, is-uint256)
		std::vector<std::tuple<std::string, std::string, bool>> stateVars;
		std::vector<EventInfo> events;
		std::vector<ErrorInfo> errors;
	};

	// ===== Visitor methods =====

	std::string visit(Program const& _p);
	std::string visitContract(ContractDef const& _c, unsigned _idx);
	std::string visitFunction(FunctionDef const& _f, ContractInfo const& _cinfo, unsigned _funcIdx);
	std::string visitBlock(Block const& _b);
	std::string visitStatement(Statement const& _s);

	// Statement visitors
	std::string visitVarDecl(VarDeclStmt const& _s);
	std::string visitExprStmt(ExprStmt const& _s);
	std::string visitIf(IfStmt const& _s);
	std::string visitFor(ForStmt const& _s);
	std::string visitWhile(WhileStmt const& _s);
	std::string visitDoWhile(DoWhileStmt const& _s);
	std::string visitReturn(ReturnStmt const& _s);
	std::string visitEmit(EmitStmt const& _s);
	std::string visitRevert(RevertStmt const& _s);
	std::string visitRequire(RequireStmt const& _s);
	std::string visitUnchecked(UncheckedBlock const& _s);

	// Expression visitors — generate uint256-typed or bool-typed expressions
	std::string visitUintExpr(Expression const& _e);
	std::string visitBoolExpr(Expression const& _e);

	// ===== Test contract =====
	std::string generateTestContract();

	// ===== Type helpers =====
	std::string elementaryTypeStr(ElementaryType const& _t);
	std::string elementaryTypeStr(TypeName const& _t);
	bool isUintType(TypeName const& _t);

	// ===== Scope management =====
	void pushScope();
	void popScope();
	/// Add a uint256 variable to the current scope.
	void addVar(std::string const& _name);
	/// Find a uint256 variable in scope, using _hint to pick one.
	/// Falls back to literal "0" if no variables are in scope.
	std::string findVar(uint32_t _hint);
	/// Find a uint256 variable that is an lvalue (local or state var).
	std::string findLVar(uint32_t _hint);
	/// Get all uint256 variables in scope (locals + params + state vars if view/nonpayable).
	std::vector<std::string> allUintVars();

	// ===== Helpers =====
	std::string indent();
	unsigned randomNumber();
	std::string defaultUintLiteral();
	std::string defaultBoolLiteral();

	// Binary/unary op classification
	static bool isArithmeticOp(BinaryOp::Op _op);
	static bool isBitwiseOp(BinaryOp::Op _op);
	static bool isComparisonOp(BinaryOp::Op _op);
	static bool isLogicalOp(BinaryOp::Op _op);
	static std::string arithmeticOpStr(BinaryOp::Op _op);
	static std::string bitwiseOpStr(BinaryOp::Op _op);
	static std::string comparisonOpStr(BinaryOp::Op _op);
	static std::string logicalOpStr(BinaryOp::Op _op);
	static std::string assignOpStr(AssignExpr::Op _op);

	// ===== Limits =====
	static constexpr unsigned s_maxExprDepth = 3;
	static constexpr unsigned s_maxStmtDepth = 3;
	static constexpr unsigned s_maxLocalVars = 10;
	static constexpr unsigned s_maxContracts = 5;
	static constexpr unsigned s_maxFunctions = 5;
	static constexpr unsigned s_maxStmtsPerBlock = 5;
	static constexpr unsigned s_maxParams = 4;
	static constexpr unsigned s_maxStateVars = 5;
	static constexpr unsigned s_maxEvents = 3;
	static constexpr unsigned s_maxErrors = 3;
	static constexpr unsigned s_maxEventParams = 3;
	static constexpr unsigned s_maxErrorParams = 3;
	static constexpr unsigned s_maxForIter = 5;

	// ===== State =====
	unsigned m_exprDepth = 0;
	unsigned m_stmtDepth = 0;
	unsigned m_indentLevel = 0;
	unsigned m_varCounter = 0;
	unsigned m_localVarCount = 0;
	bool m_inLoop = false;
	bool m_inConstructor = false;
	unsigned m_currentFuncIdx = 0;

	/// Info about all generated contracts
	std::vector<ContractInfo> m_contracts;
	/// Current contract index
	unsigned m_currentContract = 0;
	/// Current contract's uint256 state var names (available in view/nonpayable functions)
	std::vector<std::string> m_currentUintStateVars;
	/// Whether current function can read state
	bool m_canReadState = false;
	/// Current contract's events
	std::vector<EventInfo> m_currentEvents;
	/// Current contract's errors
	std::vector<ErrorInfo> m_currentErrors;
	/// Scope stack: each scope is a list of uint256 variable names
	std::vector<std::vector<std::string>> m_scopeStack;
	/// RNG
	std::shared_ptr<SolRandomNumGenerator> m_randomGen;
};

} // namespace solidity::test::sol2protofuzzer
