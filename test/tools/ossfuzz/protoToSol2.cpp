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

#include <test/tools/ossfuzz/protoToSol2.h>

#include <algorithm>

using namespace solidity::test::sol2protofuzzer;

// =====================================================================
// Top-level
// =====================================================================

std::string ProtoConverter::protoToSolidity(Program const& _p)
{
	m_randomGen = std::make_shared<SolRandomNumGenerator>(
		static_cast<unsigned>(_p.seed())
	);
	return visit(_p);
}

std::string ProtoConverter::visit(Program const& _p)
{
	// Pre-process: create ContractInfo for each contract
	unsigned numContracts = std::min(
		static_cast<unsigned>(_p.contracts_size()),
		s_maxContracts
	);

	for (unsigned i = 0; i < numContracts; i++)
	{
		auto const& c = _p.contracts(i);
		ContractInfo info;
		// Reserve "C" for the test contract
		info.name = "C" + std::to_string(i);
		info.kind = c.kind();

		// Functions
		unsigned numFuncs = std::min(
			static_cast<unsigned>(c.functions_size()),
			s_maxFunctions
		);
		for (unsigned j = 0; j < numFuncs; j++)
		{
			FuncInfo fi;
			fi.name = "f" + std::to_string(j);
			fi.numParams = std::min(
				static_cast<unsigned>(c.functions(j).params_size()),
				s_maxParams
			);
			fi.vis = c.functions(j).vis();
			fi.mut = c.functions(j).mut();
			info.functions.push_back(fi);
		}

		// State variables
		unsigned numSV = std::min(
			static_cast<unsigned>(c.state_vars_size()),
			s_maxStateVars
		);
		for (unsigned j = 0; j < numSV; j++)
		{
			auto const& sv = c.state_vars(j);
			std::string svName = "sv" + std::to_string(j);
			std::string svType = elementaryTypeStr(sv.type());
			bool isUint = isUintType(sv.type());
			info.stateVars.emplace_back(svName, svType, isUint);
		}

		// Events
		unsigned numEv = std::min(
			static_cast<unsigned>(c.events_size()),
			s_maxEvents
		);
		for (unsigned j = 0; j < numEv; j++)
		{
			EventInfo ei;
			ei.name = "Ev" + std::to_string(i) + "_" + std::to_string(j);
			ei.numParams = std::min(
				static_cast<unsigned>(c.events(j).params_size()),
				s_maxEventParams
			);
			if (ei.numParams == 0)
				ei.numParams = 1;
			info.events.push_back(ei);
		}

		// Errors
		unsigned numErr = std::min(
			static_cast<unsigned>(c.errors_size()),
			s_maxErrors
		);
		for (unsigned j = 0; j < numErr; j++)
		{
			ErrorInfo eri;
			eri.name = "Err" + std::to_string(i) + "_" + std::to_string(j);
			eri.numParams = std::min(
				static_cast<unsigned>(c.errors(j).params_size()),
				s_maxErrorParams
			);
			if (eri.numParams == 0)
				eri.numParams = 1;
			info.errors.push_back(eri);
		}

		m_contracts.push_back(info);
	}

	// Generate source
	std::ostringstream o;
	o << "// SPDX-License-Identifier: GPL-3.0\n";
	o << "pragma solidity >=0.0;\n\n";

	// Generate contracts
	for (unsigned i = 0; i < numContracts; i++)
		o << visitContract(_p.contracts(i), i) << "\n";

	// Generate test contract
	o << generateTestContract();

	return o.str();
}

// =====================================================================
// Contract generation
// =====================================================================

std::string ProtoConverter::visitContract(ContractDef const& _c, unsigned _idx)
{
	auto const& info = m_contracts[_idx];
	m_currentContract = _idx;

	bool isLibrary = (info.kind == ContractDef::LIBRARY);
	// For v1, treat interfaces and abstract contracts as regular contracts
	// (interfaces would need special handling for bodyless functions)

	std::ostringstream o;
	o << (isLibrary ? "library " : "contract ") << info.name << " {\n";

	// State variables (skip for libraries)
	if (!isLibrary)
	{
		for (auto const& [name, typeStr, _isUint] : info.stateVars)
			o << "\t" << typeStr << " public " << name << ";\n";
		if (!info.stateVars.empty())
			o << "\n";
	}

	// Events
	for (auto const& ev : info.events)
	{
		o << "\tevent " << ev.name << "(";
		for (unsigned j = 0; j < ev.numParams; j++)
		{
			if (j > 0) o << ", ";
			o << "uint256";
		}
		o << ");\n";
	}

	// Errors
	for (auto const& err : info.errors)
	{
		o << "\terror " << err.name << "(";
		for (unsigned j = 0; j < err.numParams; j++)
		{
			if (j > 0) o << ", ";
			o << "uint256";
		}
		o << ");\n";
	}

	if (!info.events.empty() || !info.errors.empty())
		o << "\n";

	// Constructor
	if (!isLibrary && _c.has_constructor())
	{
		auto const& ctor = _c.constructor();
		o << "\tconstructor() ";
		if (ctor.payable())
			o << "payable ";
		o << "{\n";

		// Set up state for constructor body
		m_canReadState = true;
		m_inConstructor = true;
		m_currentFuncIdx = 0;
		m_currentUintStateVars.clear();
		for (auto const& [name, _typeStr, isUint] : info.stateVars)
		{
			if (isUint)
				m_currentUintStateVars.push_back(name);
		}
		m_currentEvents = info.events;
		m_currentErrors = info.errors;

		pushScope();
		m_localVarCount = 0;
		m_varCounter = 0;
		m_indentLevel = 2;
		m_stmtDepth = 0;
		o << visitBlock(ctor.body());
		popScope();
		m_inConstructor = false;

		o << "\t}\n\n";
	}

	// Functions
	for (unsigned j = 0; j < info.functions.size(); j++)
		o << visitFunction(_c.functions(j), info, j) << "\n";

	o << "}\n";
	return o.str();
}

// =====================================================================
// Function generation
// =====================================================================

std::string ProtoConverter::visitFunction(
	FunctionDef const& _f,
	ContractInfo const& _cinfo,
	unsigned _funcIdx
)
{
	auto const& fi = _cinfo.functions[_funcIdx];
	m_currentFuncIdx = _funcIdx;
	bool isLibrary = (_cinfo.kind == ContractDef::LIBRARY);

	// Determine visibility
	std::string vis;
	if (isLibrary)
		vis = "internal";
	else
	{
		switch (fi.vis)
		{
		case PUBLIC: vis = "public"; break;
		case EXTERNAL: vis = "external"; break;
		case INTERNAL: vis = "internal"; break;
		case PRIVATE: vis = "private"; break;
		}
	}

	// Determine mutability
	std::string mut;
	StateMutability actualMut = fi.mut;
	// Libraries can't be payable
	if (isLibrary && actualMut == PAYABLE)
		actualMut = PURE;
	// External/public functions that are payable need special care
	switch (actualMut)
	{
	case PURE: mut = "pure"; break;
	case VIEW: mut = "view"; break;
	case PAYABLE: mut = "payable"; break;
	case NONPAYABLE: mut = ""; break;
	}

	// Set up state access
	m_canReadState = (actualMut == VIEW || actualMut == NONPAYABLE || actualMut == PAYABLE);
	m_currentUintStateVars.clear();
	if (m_canReadState && !isLibrary)
	{
		for (auto const& [name, _typeStr, isUint] : _cinfo.stateVars)
		{
			if (isUint)
				m_currentUintStateVars.push_back(name);
		}
	}

	// Set events/errors for current contract
	m_currentEvents = _cinfo.events;
	m_currentErrors = _cinfo.errors;

	// Push scope and add params
	pushScope();
	m_localVarCount = 0;
	m_varCounter = 0;
	for (unsigned i = 0; i < fi.numParams; i++)
		addVar("p" + std::to_string(i));

	// Generate body
	m_indentLevel = 2;
	m_stmtDepth = 0;
	std::string body = visitBlock(_f.body());

	popScope();

	// Build function string
	std::ostringstream o;
	o << "\tfunction " << fi.name << "(";
	for (unsigned i = 0; i < fi.numParams; i++)
	{
		if (i > 0) o << ", ";
		o << "uint256 p" << i;
	}
	o << ") " << vis;
	if (!mut.empty())
		o << " " << mut;
	o << " returns (uint256) {\n";
	o << body;
	// Always return something to ensure the function compiles
	o << "\t\treturn 0;\n";
	o << "\t}\n";

	return o.str();
}

// =====================================================================
// Block and statement generation
// =====================================================================

std::string ProtoConverter::visitBlock(Block const& _b)
{
	pushScope();
	std::ostringstream o;

	unsigned numStmts = std::min(
		static_cast<unsigned>(_b.stmts_size()),
		s_maxStmtsPerBlock
	);
	for (unsigned i = 0; i < numStmts; i++)
		o << visitStatement(_b.stmts(i));

	popScope();
	return o.str();
}

std::string ProtoConverter::visitStatement(Statement const& _s)
{
	if (m_stmtDepth >= s_maxStmtDepth)
		return "";

	m_stmtDepth++;
	std::string result;

	switch (_s.stmt_oneof_case())
	{
	case Statement::kVarDecl:
		result = visitVarDecl(_s.var_decl());
		break;
	case Statement::kExprStmt:
		result = visitExprStmt(_s.expr_stmt());
		break;
	case Statement::kIfStmt:
		result = visitIf(_s.if_stmt());
		break;
	case Statement::kForStmt:
		result = visitFor(_s.for_stmt());
		break;
	case Statement::kWhileStmt:
		result = visitWhile(_s.while_stmt());
		break;
	case Statement::kDoWhile:
		result = visitDoWhile(_s.do_while());
		break;
	case Statement::kReturnStmt:
		result = visitReturn(_s.return_stmt());
		break;
	case Statement::kEmitStmt:
		result = visitEmit(_s.emit_stmt());
		break;
	case Statement::kRevertStmt:
		// Skip revert in generated code to avoid unexpected reverts
		// during execution. Generate a no-op expression instead.
		result = "";
		break;
	case Statement::kBlock:
	{
		std::ostringstream o;
		o << indent() << "{\n";
		m_indentLevel++;
		o << visitBlock(_s.block());
		m_indentLevel--;
		o << indent() << "}\n";
		result = o.str();
		break;
	}
	case Statement::kUnchecked:
		result = visitUnchecked(_s.unchecked());
		break;
	case Statement::kBreakStmt:
		if (m_inLoop)
			result = indent() + "break;\n";
		break;
	case Statement::kContinueStmt:
		if (m_inLoop)
			result = indent() + "continue;\n";
		break;
	case Statement::kRequireStmt:
		result = visitRequire(_s.require_stmt());
		break;
	default:
		break;
	}

	m_stmtDepth--;
	return result;
}

std::string ProtoConverter::visitVarDecl(VarDeclStmt const& _s)
{
	if (m_localVarCount >= s_maxLocalVars)
		return "";

	std::string varName = "v" + std::to_string(m_varCounter++);
	addVar(varName);
	m_localVarCount++;

	std::ostringstream o;
	o << indent() << "uint256 " << varName;
	if (_s.has_init())
		o << " = " << visitUintExpr(_s.init());
	else
		o << " = 0";
	o << ";\n";
	return o.str();
}

std::string ProtoConverter::visitExprStmt(ExprStmt const& _s)
{
	// Generate a uint expression as a statement (useful for assignments, function calls)
	std::string expr = visitUintExpr(_s.expr());
	if (expr.empty())
		return "";
	return indent() + expr + ";\n";
}

std::string ProtoConverter::visitIf(IfStmt const& _s)
{
	std::ostringstream o;
	o << indent() << "if (" << visitBoolExpr(_s.cond()) << ") {\n";
	m_indentLevel++;
	o << visitBlock(_s.if_body());
	m_indentLevel--;
	if (_s.has_else_body())
	{
		o << indent() << "} else {\n";
		m_indentLevel++;
		o << visitBlock(_s.else_body());
		m_indentLevel--;
	}
	o << indent() << "}\n";
	return o.str();
}

std::string ProtoConverter::visitFor(ForStmt const& _s)
{
	// Always generate a bounded for loop to prevent infinite loops
	std::string iterVar = "i" + std::to_string(m_varCounter++);
	unsigned bound = s_maxForIter;

	std::ostringstream o;
	o << indent() << "for (uint256 " << iterVar << " = 0; "
	  << iterVar << " < " << bound << "; "
	  << iterVar << "++) {\n";

	pushScope();
	addVar(iterVar);
	m_localVarCount++;

	bool wasInLoop = m_inLoop;
	m_inLoop = true;
	m_indentLevel++;
	o << visitBlock(_s.body());
	m_indentLevel--;
	m_inLoop = wasInLoop;

	popScope();
	o << indent() << "}\n";
	return o.str();
}

std::string ProtoConverter::visitWhile(WhileStmt const& _s)
{
	// Bounded while loop with a counter
	std::string counterVar = "w" + std::to_string(m_varCounter++);
	unsigned bound = s_maxForIter;

	std::ostringstream o;
	o << indent() << "{\n";
	m_indentLevel++;
	o << indent() << "uint256 " << counterVar << " = 0;\n";

	pushScope();
	addVar(counterVar);
	m_localVarCount++;

	o << indent() << "while (" << visitBoolExpr(_s.cond())
	  << " && " << counterVar << " < " << bound << ") {\n";

	bool wasInLoop = m_inLoop;
	m_inLoop = true;
	m_indentLevel++;
	o << indent() << counterVar << "++;\n";
	o << visitBlock(_s.body());
	m_indentLevel--;
	m_inLoop = wasInLoop;

	o << indent() << "}\n";
	popScope();
	m_indentLevel--;
	o << indent() << "}\n";
	return o.str();
}

std::string ProtoConverter::visitDoWhile(DoWhileStmt const& _s)
{
	// Bounded do-while
	std::string counterVar = "d" + std::to_string(m_varCounter++);
	unsigned bound = s_maxForIter;

	std::ostringstream o;
	o << indent() << "{\n";
	m_indentLevel++;
	o << indent() << "uint256 " << counterVar << " = 0;\n";

	pushScope();
	addVar(counterVar);
	m_localVarCount++;

	o << indent() << "do {\n";

	bool wasInLoop = m_inLoop;
	m_inLoop = true;
	m_indentLevel++;
	o << indent() << counterVar << "++;\n";
	o << visitBlock(_s.body());
	m_indentLevel--;
	m_inLoop = wasInLoop;

	o << indent() << "} while (" << visitBoolExpr(_s.cond())
	  << " && " << counterVar << " < " << bound << ");\n";
	popScope();
	m_indentLevel--;
	o << indent() << "}\n";
	return o.str();
}

std::string ProtoConverter::visitReturn(ReturnStmt const& _s)
{
	std::ostringstream o;
	if (_s.has_val())
		o << indent() << "return " << visitUintExpr(_s.val()) << ";\n";
	else
		o << indent() << "return 0;\n";
	return o.str();
}

std::string ProtoConverter::visitEmit(EmitStmt const& _s)
{
	if (m_currentEvents.empty())
		return "";

	unsigned evIdx = _s.event_id() % m_currentEvents.size();
	auto const& ev = m_currentEvents[evIdx];

	std::ostringstream o;
	o << indent() << "emit " << ev.name << "(";
	for (unsigned i = 0; i < ev.numParams; i++)
	{
		if (i > 0) o << ", ";
		if (i < static_cast<unsigned>(_s.args_size()))
			o << visitUintExpr(_s.args(i));
		else
			o << "0";
	}
	o << ");\n";
	return o.str();
}

std::string ProtoConverter::visitRevert(RevertStmt const& _s)
{
	if (m_currentErrors.empty())
		return "";

	unsigned errIdx = _s.error_id() % m_currentErrors.size();
	auto const& err = m_currentErrors[errIdx];

	std::ostringstream o;
	o << indent() << "revert " << err.name << "(";
	for (unsigned i = 0; i < err.numParams; i++)
	{
		if (i > 0) o << ", ";
		if (i < static_cast<unsigned>(_s.args_size()))
			o << visitUintExpr(_s.args(i));
		else
			o << "0";
	}
	o << ");\n";
	return o.str();
}

std::string ProtoConverter::visitRequire(RequireStmt const& _s)
{
	// Skip require/assert in constructors to avoid reverting during
	// contract creation (test contract uses `new` which propagates reverts)
	if (m_inConstructor)
		return "";

	std::string cond = visitBoolExpr(_s.cond());
	if (_s.is_assert())
		return indent() + "assert(" + cond + ");\n";
	else
		return indent() + "require(" + cond + ");\n";
}

std::string ProtoConverter::visitUnchecked(UncheckedBlock const& _s)
{
	std::ostringstream o;
	o << indent() << "unchecked {\n";
	m_indentLevel++;
	o << visitBlock(_s.body());
	m_indentLevel--;
	o << indent() << "}\n";
	return o.str();
}

// =====================================================================
// Expression generation
// =====================================================================

std::string ProtoConverter::visitUintExpr(Expression const& _e)
{
	if (m_exprDepth >= s_maxExprDepth)
		return findVar(0);

	m_exprDepth++;
	std::string result;

	switch (_e.expr_oneof_case())
	{
	case Expression::kLit:
	{
		auto const& lit = _e.lit();
		if (lit.has_int_lit())
			result = std::to_string(lit.int_lit().val() % 1000);
		else if (lit.has_bool_lit())
			result = lit.bool_lit().val() ? "1" : "0";
		else if (lit.has_addr_lit())
		{
			// Generate a deterministic checksummed address as a uint
			// Use the seed to produce a non-zero address value
			uint64_t v = lit.addr_lit().val();
			result = "uint256(uint160(" + std::to_string(v % 1000000) + "))";
		}
		else if (lit.has_str_lit())
		{
			// Generate a deterministic string literal hashed to uint256
			uint32_t seed = lit.str_lit().seed();
			result = "uint256(keccak256(bytes(\"s" + std::to_string(seed % 100) + "\")))";
		}
		else
			result = defaultUintLiteral();
		break;
	}
	case Expression::kVarRef:
		result = findVar(_e.var_ref().index());
		break;
	case Expression::kBinOp:
	{
		auto const& op = _e.bin_op();
		if (isArithmeticOp(op.op()))
		{
			std::string left = visitUintExpr(op.left());
			std::string right = visitUintExpr(op.right());
			// Make division/modulo safe: use (right | 1) to avoid div-by-zero
			if (op.op() == BinaryOp::DIV || op.op() == BinaryOp::MOD)
				result = left + " " + arithmeticOpStr(op.op()) + " (" + right + " | 1)";
			else
				result = left + " " + arithmeticOpStr(op.op()) + " " + right;
		}
		else if (isBitwiseOp(op.op()))
		{
			result = visitUintExpr(op.left()) + " " +
				bitwiseOpStr(op.op()) + " " +
				visitUintExpr(op.right());
		}
		else
		{
			// Comparison or logical op used in uint context: wrap in ternary
			result = "(" + visitBoolExpr(_e) + " ? uint256(1) : uint256(0))";
		}
		break;
	}
	case Expression::kUnOp:
	{
		auto const& op = _e.un_op();
		switch (op.op())
		{
		case UnaryOp::BNOT:
			result = "~" + visitUintExpr(op.operand());
			break;
		case UnaryOp::NEG:
			// Unary minus on uint can be problematic, use bitwise not instead
			result = "~" + visitUintExpr(op.operand());
			break;
		case UnaryOp::INC_PRE:
		{
			std::string v = findLVar(op.operand().has_var_ref() ? op.operand().var_ref().index() : 0);
			result = "++" + v;
			break;
		}
		case UnaryOp::DEC_PRE:
		{
			std::string v = findLVar(op.operand().has_var_ref() ? op.operand().var_ref().index() : 0);
			result = "--" + v;
			break;
		}
		case UnaryOp::INC_POST:
		{
			std::string v = findLVar(op.operand().has_var_ref() ? op.operand().var_ref().index() : 0);
			result = v + "++";
			break;
		}
		case UnaryOp::DEC_POST:
		{
			std::string v = findLVar(op.operand().has_var_ref() ? op.operand().var_ref().index() : 0);
			result = v + "--";
			break;
		}
		default:
			result = defaultUintLiteral();
		}
		break;
	}
	case Expression::kTernary:
		result = "(" + visitBoolExpr(_e.ternary().cond()) + " ? " +
			visitUintExpr(_e.ternary().true_val()) + " : " +
			visitUintExpr(_e.ternary().false_val()) + ")";
		break;
	case Expression::kMsgExpr:
		switch (_e.msg_expr().field())
		{
		case MsgExpr::SENDER:
			result = "uint256(uint160(msg.sender))";
			break;
		case MsgExpr::VALUE:
			result = "msg.value";
			break;
		}
		break;
	case Expression::kBlockExpr:
		switch (_e.block_expr().field())
		{
		case BlockExpr::TIMESTAMP:
			result = "block.timestamp";
			break;
		case BlockExpr::NUMBER:
			result = "block.number";
			break;
		case BlockExpr::CHAINID:
			result = "block.chainid";
			break;
		case BlockExpr::BASEFEE:
			result = "block.basefee";
			break;
		case BlockExpr::PREVRANDAO:
			result = "block.prevrandao";
			break;
		case BlockExpr::GASLIMIT:
			result = "block.gaslimit";
			break;
		}
		break;
	case Expression::kTxExpr:
		switch (_e.tx_expr().field())
		{
		case TxExpr::ORIGIN:
			result = "uint256(uint160(tx.origin))";
			break;
		case TxExpr::GASPRICE:
			result = "tx.gasprice";
			break;
		}
		break;
	case Expression::kHashExpr:
	{
		auto const& h = _e.hash_expr();
		std::string inner = visitUintExpr(h.arg());
		if (h.kind() == HashExpr::KECCAK256)
			result = "uint256(keccak256(abi.encode(" + inner + ")))";
		else
			result = "uint256(sha256(abi.encode(" + inner + ")))";
		break;
	}
	case Expression::kMathExpr:
	{
		auto const& m = _e.math_expr();
		std::string x = visitUintExpr(m.x());
		std::string y = visitUintExpr(m.y());
		std::string mod = "(" + visitUintExpr(m.mod()) + " | 1)";
		if (m.kind() == MathExpr::ADDMOD)
			result = "addmod(" + x + ", " + y + ", " + mod + ")";
		else
			result = "mulmod(" + x + ", " + y + ", " + mod + ")";
		break;
	}
	case Expression::kBuiltin:
		result = "gasleft()";
		break;
	case Expression::kAssign:
	{
		auto const& a = _e.assign();
		std::string lhs = findLVar(a.lhs().index());
		std::string rhs = visitUintExpr(a.rhs());
		if (a.op() == AssignExpr::DIV_ASSIGN || a.op() == AssignExpr::MOD_ASSIGN)
		{
			// Avoid div-by-zero: do regular assignment with safe division
			if (a.op() == AssignExpr::DIV_ASSIGN)
				result = "(" + lhs + " = " + lhs + " / (" + rhs + " | 1))";
			else
				result = "(" + lhs + " = " + lhs + " % (" + rhs + " | 1))";
		}
		else
			result = "(" + lhs + " " + assignOpStr(a.op()) + " " + rhs + ")";
		break;
	}
	case Expression::kFuncCall:
	{
		auto const& fc = _e.func_call();
		auto const& cinfo = m_contracts[m_currentContract];
		// Only call functions with lower index to avoid recursion
		unsigned callableCount = std::min(
			m_currentFuncIdx,
			static_cast<unsigned>(cinfo.functions.size())
		);
		if (callableCount > 0)
		{
			unsigned targetIdx = fc.func_id() % callableCount;
			auto const& target = cinfo.functions[targetIdx];
			// Skip external functions (need this. prefix which changes context)
			if (target.vis != EXTERNAL)
			{
				std::ostringstream call;
				call << target.name << "(";
				for (unsigned i = 0; i < target.numParams; i++)
				{
					if (i > 0) call << ", ";
					if (i < static_cast<unsigned>(fc.args_size()))
						call << visitUintExpr(fc.args(i));
					else
						call << "0";
				}
				call << ")";
				result = call.str();
				break;
			}
		}
		result = findVar(randomNumber());
		break;
	}
	case Expression::kTypeConv:
	{
		auto const& tc = _e.type_conv();
		std::string inner = visitUintExpr(tc.arg());
		auto const& toType = tc.to_type();
		if (toType.type_oneof_case() == ElementaryType::kIntType)
		{
			static const unsigned widths[] = {8, 16, 32, 64, 128, 256};
			unsigned w = widths[toType.int_type().width() % 6];
			if (toType.int_type().is_signed())
				// Signed round-trip: uint256 -> intN -> int256 -> uint256
				result = "uint256(int256(int" + std::to_string(w) + "(" + inner + ")))";
			else if (w < 256)
				// Unsigned narrowing + widening
				result = "uint256(uint" + std::to_string(w) + "(" + inner + "))";
			else
				result = inner;
		}
		else
			result = inner;
		break;
	}
	case Expression::kIndexAccess:
	case Expression::kAbiEncode:
	default:
		result = findVar(randomNumber());
		break;
	}

	m_exprDepth--;
	return result.empty() ? defaultUintLiteral() : result;
}

std::string ProtoConverter::visitBoolExpr(Expression const& _e)
{
	if (m_exprDepth >= s_maxExprDepth)
		return defaultBoolLiteral();

	m_exprDepth++;
	std::string result;

	switch (_e.expr_oneof_case())
	{
	case Expression::kLit:
	{
		auto const& lit = _e.lit();
		if (lit.has_bool_lit())
			result = lit.bool_lit().val() ? "true" : "false";
		else
			result = defaultBoolLiteral();
		break;
	}
	case Expression::kBinOp:
	{
		auto const& op = _e.bin_op();
		if (isComparisonOp(op.op()))
		{
			result = visitUintExpr(op.left()) + " " +
				comparisonOpStr(op.op()) + " " +
				visitUintExpr(op.right());
		}
		else if (isLogicalOp(op.op()))
		{
			result = visitBoolExpr(op.left()) + " " +
				logicalOpStr(op.op()) + " " +
				visitBoolExpr(op.right());
		}
		else
		{
			// Arithmetic/bitwise used in bool context: convert via comparison
			result = visitUintExpr(_e) + " != 0";
		}
		break;
	}
	case Expression::kUnOp:
	{
		auto const& op = _e.un_op();
		if (op.op() == UnaryOp::LNOT)
			result = "!" + visitBoolExpr(op.operand());
		else
			// Non-logical unary in bool context: compare result to 0
			result = visitUintExpr(_e) + " != 0";
		break;
	}
	case Expression::kTernary:
		result = "(" + visitBoolExpr(_e.ternary().cond()) + " ? " +
			visitBoolExpr(_e.ternary().true_val()) + " : " +
			visitBoolExpr(_e.ternary().false_val()) + ")";
		break;
	default:
		// For any other expression type, generate a comparison
		result = visitUintExpr(_e) + " != 0";
		break;
	}

	m_exprDepth--;
	return result.empty() ? defaultBoolLiteral() : result;
}

// =====================================================================
// Test contract generation
// =====================================================================

std::string ProtoConverter::generateTestContract()
{
	std::ostringstream o;
	o << "contract C {\n";
	o << "\tfunction test() public returns (uint256) {\n";

	// For each non-library contract, create an instance and call its functions
	for (auto const& ci : m_contracts)
	{
		if (ci.kind == ContractDef::LIBRARY)
			continue;
		if (ci.functions.empty())
			continue;

		// Create instance
		o << "\t\t" << ci.name << " _t" << ci.name
		  << " = new " << ci.name << "();\n";

		// Call each public/external function via low-level call
		for (auto const& fi : ci.functions)
		{
			if (fi.vis != PUBLIC && fi.vis != EXTERNAL)
				continue;

			// Build signature string
			std::string sig = fi.name + "(";
			for (unsigned i = 0; i < fi.numParams; i++)
			{
				if (i > 0) sig += ",";
				sig += "uint256";
			}
			sig += ")";

			o << "\t\t(bool _s" << ci.name << fi.name << ", ) = address(_t"
			  << ci.name << ").call(abi.encodeWithSignature(\""
			  << sig << "\"";
			for (unsigned i = 0; i < fi.numParams; i++)
				o << ", uint256(" << (i + 1) << ")";
			o << "));\n";
		}
	}

	// For library contracts, call functions directly
	for (auto const& ci : m_contracts)
	{
		if (ci.kind != ContractDef::LIBRARY)
			continue;

		for (auto const& fi : ci.functions)
		{
			o << "\t\t" << ci.name << "." << fi.name << "(";
			for (unsigned i = 0; i < fi.numParams; i++)
			{
				if (i > 0) o << ", ";
				o << "uint256(" << (i + 1) << ")";
			}
			o << ");\n";
		}
	}

	o << "\t\treturn 0;\n";
	o << "\t}\n";
	o << "}\n";
	return o.str();
}

// =====================================================================
// Type helpers
// =====================================================================

std::string ProtoConverter::elementaryTypeStr(ElementaryType const& _t)
{
	switch (_t.type_oneof_case())
	{
	case ElementaryType::kBoolType:
		return "bool";
	case ElementaryType::kIntType:
	{
		static const unsigned widths[] = {8, 16, 32, 64, 128, 256};
		unsigned w = widths[_t.int_type().width() % 6];
		return (_t.int_type().is_signed() ? "int" : "uint") + std::to_string(w);
	}
	case ElementaryType::kAddressPayable:
		return _t.address_payable() ? "address payable" : "address";
	case ElementaryType::kFixedBytes:
	{
		static const unsigned widths[] = {1, 2, 4, 8, 16, 32};
		unsigned w = widths[_t.fixed_bytes().width() % 6];
		return "bytes" + std::to_string(w);
	}
	case ElementaryType::kIsString:
		return _t.is_string() ? "string" : "bytes";
	default:
		return "uint256";
	}
}

std::string ProtoConverter::elementaryTypeStr(TypeName const& _t)
{
	switch (_t.type_oneof_case())
	{
	case TypeName::kElementary:
		return elementaryTypeStr(_t.elementary());
	case TypeName::kArray:
	{
		std::string base = elementaryTypeStr(_t.array().base());
		if (_t.array().has_length())
		{
			unsigned len = std::max(1u, _t.array().length() % 10);
			return base + "[" + std::to_string(len) + "]";
		}
		return base + "[]";
	}
	case TypeName::kMapping:
	{
		std::string key = elementaryTypeStr(_t.mapping().key());
		std::string val = elementaryTypeStr(_t.mapping().value());
		// Mapping keys must be elementary (not string/bytes/dynamic)
		// Simplify: use uint256 for key if the type is dynamic
		if (key == "string" || key == "bytes")
			key = "uint256";
		return "mapping(" + key + " => " + val + ")";
	}
	default:
		return "uint256";
	}
}

bool ProtoConverter::isUintType(TypeName const& _t)
{
	if (_t.type_oneof_case() != TypeName::kElementary)
		return false;
	auto const& e = _t.elementary();
	if (e.type_oneof_case() != ElementaryType::kIntType)
		return false;
	return !e.int_type().is_signed();
}

// =====================================================================
// Scope management
// =====================================================================

void ProtoConverter::pushScope()
{
	m_scopeStack.emplace_back();
}

void ProtoConverter::popScope()
{
	if (!m_scopeStack.empty())
		m_scopeStack.pop_back();
}

void ProtoConverter::addVar(std::string const& _name)
{
	if (!m_scopeStack.empty())
		m_scopeStack.back().push_back(_name);
}

std::vector<std::string> ProtoConverter::allUintVars()
{
	std::vector<std::string> vars;
	for (auto const& scope : m_scopeStack)
		for (auto const& v : scope)
			vars.push_back(v);
	if (m_canReadState)
		for (auto const& sv : m_currentUintStateVars)
			vars.push_back(sv);
	return vars;
}

std::string ProtoConverter::findVar(uint32_t _hint)
{
	auto vars = allUintVars();
	if (vars.empty())
		return defaultUintLiteral();
	return vars[_hint % vars.size()];
}

std::string ProtoConverter::findLVar(uint32_t _hint)
{
	// Only local variables (not state vars) for lvalue operations like ++/--
	std::vector<std::string> vars;
	for (auto const& scope : m_scopeStack)
		for (auto const& v : scope)
			vars.push_back(v);
	if (vars.empty())
		return defaultUintLiteral();
	return vars[_hint % vars.size()];
}

// =====================================================================
// Helpers
// =====================================================================

std::string ProtoConverter::indent()
{
	return std::string(m_indentLevel, '\t');
}

unsigned ProtoConverter::randomNumber()
{
	if (m_randomGen)
		return m_randomGen->operator()();
	return 0;
}

std::string ProtoConverter::defaultUintLiteral()
{
	return "0";
}

std::string ProtoConverter::defaultBoolLiteral()
{
	return "true";
}

// =====================================================================
// Operator classification and stringification
// =====================================================================

bool ProtoConverter::isArithmeticOp(BinaryOp::Op _op)
{
	return _op == BinaryOp::ADD || _op == BinaryOp::SUB ||
		_op == BinaryOp::MUL || _op == BinaryOp::DIV ||
		_op == BinaryOp::MOD || _op == BinaryOp::EXP;
}

bool ProtoConverter::isBitwiseOp(BinaryOp::Op _op)
{
	return _op == BinaryOp::BIT_AND || _op == BinaryOp::BIT_OR ||
		_op == BinaryOp::BIT_XOR || _op == BinaryOp::SHL ||
		_op == BinaryOp::SHR;
}

bool ProtoConverter::isComparisonOp(BinaryOp::Op _op)
{
	return _op == BinaryOp::LT || _op == BinaryOp::GT ||
		_op == BinaryOp::LTE || _op == BinaryOp::GTE ||
		_op == BinaryOp::EQ || _op == BinaryOp::NEQ;
}

bool ProtoConverter::isLogicalOp(BinaryOp::Op _op)
{
	return _op == BinaryOp::AND || _op == BinaryOp::OR;
}

std::string ProtoConverter::arithmeticOpStr(BinaryOp::Op _op)
{
	switch (_op)
	{
	case BinaryOp::ADD: return "+";
	case BinaryOp::SUB: return "-";
	case BinaryOp::MUL: return "*";
	case BinaryOp::DIV: return "/";
	case BinaryOp::MOD: return "%";
	case BinaryOp::EXP: return "**";
	default: return "+";
	}
}

std::string ProtoConverter::bitwiseOpStr(BinaryOp::Op _op)
{
	switch (_op)
	{
	case BinaryOp::BIT_AND: return "&";
	case BinaryOp::BIT_OR: return "|";
	case BinaryOp::BIT_XOR: return "^";
	case BinaryOp::SHL: return "<<";
	case BinaryOp::SHR: return ">>";
	default: return "&";
	}
}

std::string ProtoConverter::comparisonOpStr(BinaryOp::Op _op)
{
	switch (_op)
	{
	case BinaryOp::LT: return "<";
	case BinaryOp::GT: return ">";
	case BinaryOp::LTE: return "<=";
	case BinaryOp::GTE: return ">=";
	case BinaryOp::EQ: return "==";
	case BinaryOp::NEQ: return "!=";
	default: return "==";
	}
}

std::string ProtoConverter::logicalOpStr(BinaryOp::Op _op)
{
	switch (_op)
	{
	case BinaryOp::AND: return "&&";
	case BinaryOp::OR: return "||";
	default: return "&&";
	}
}

std::string ProtoConverter::assignOpStr(AssignExpr::Op _op)
{
	switch (_op)
	{
	case AssignExpr::ASSIGN: return "=";
	case AssignExpr::ADD_ASSIGN: return "+=";
	case AssignExpr::SUB_ASSIGN: return "-=";
	case AssignExpr::MUL_ASSIGN: return "*=";
	case AssignExpr::DIV_ASSIGN: return "/=";
	case AssignExpr::MOD_ASSIGN: return "%=";
	case AssignExpr::AND_ASSIGN: return "&=";
	case AssignExpr::OR_ASSIGN: return "|=";
	case AssignExpr::XOR_ASSIGN: return "^=";
	case AssignExpr::SHL_ASSIGN: return "<<=";
	case AssignExpr::SHR_ASSIGN: return ">>=";
	default: return "=";
	}
}
