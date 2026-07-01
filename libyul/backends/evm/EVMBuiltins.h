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

#include <libyul/backends/evm/AbstractAssembly.h>

#include <libyul/Dialect.h>
#include <libyul/LiteralValue.h>
#include <libyul/Scope.h>

#include <bitset>
#include <cstddef>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace solidity::yul
{

class Object;

/// Context used during code generation.
struct BuiltinContext
{
	Object const* currentObject = nullptr;
	/// Mapping from named objects to abstract assembly sub IDs.
	std::map<std::string, AbstractAssembly::SubID> subIDs;

	std::map<Scope::Function const*, AbstractAssembly::FunctionID> functionIDs;
};

struct BuiltinFunctionForEVM: public BuiltinFunction
{
	std::optional<evmasm::Instruction> instruction;
	/// Function to generate code for a call to this builtin and append it to the abstract assembly.
	/// Receives the values of the literal-kind arguments (in argument order); all non-literal arguments
	/// are expected to be on stack in reverse order (i.e. right-most argument pushed first).
	/// Expects the caller to set the source location.
	std::function<void(std::vector<LiteralValue> const&, AbstractAssembly&, BuiltinContext&)> generateCode;
};

/// Appends the code for a call to @a _builtin to @a _assembly. For builtins that map to a single EVM
/// instruction this appends it directly (from the `instruction` field), avoiding the generateCode
/// indirection; all other builtins are emitted via generateCode. @a _literalArguments carries the values
/// of the literal-kind arguments; all non-literal arguments are expected to already be on the stack.
void generateBuiltinCode(
	BuiltinFunctionForEVM const& _builtin,
	std::vector<LiteralValue> const& _literalArguments,
	AbstractAssembly& _assembly,
	BuiltinContext& _context
);

/// Collection of all possible EVM builtin functions.
/// Each builtin can have one (or multiple) scopes, which define whether, e.g., it requires object access.
/// Using this class as single source of truth for builtin functions makes sure that these are consistent over
/// EVM dialects. If the order were to depend on the EVM dialect - which can easily happen using conditionals -,
/// different dialects' builtin handles become inherently incompatible.
class EVMBuiltins
{
	static std::size_t constexpr instructionBit = 0;
	static std::size_t constexpr replacedInstructionBit = 1;
	static std::size_t constexpr objectAccessBit = 2;

public:
	struct Scopes
	{
		/// whether the corresponding evm builtin function is an instruction builtin
		bool instruction() const { return value.test(instructionBit); }
		/// whether the corresponding evm builtin has been replaced by another builtin, ie, should be skipped
		bool replaced() const { return value.test(replacedInstructionBit); }
		/// if true, the evm builtin function is only valid when object access is given
		bool requiresObjectAccess() const { return value.test(objectAccessBit); }

		Scopes operator|(Scopes const& _other) const
		{
			Scopes result = *this;
			result |= _other;
			return result;
		}

		Scopes& operator|=(Scopes const& _other)
		{
			value |= _other.value;
			return *this;
		}

		std::bitset<3> value;
	};

	EVMBuiltins();

	std::vector<std::tuple<Scopes, BuiltinFunctionForEVM>> const& functions() const { return m_scopesAndFunctions; }

	/// Creates a verbatim builtin function. These are not part of the usual builtin functions collection and
	/// must be cached in the dialect creating them.
	static BuiltinFunctionForEVM createVerbatimFunction(size_t _arguments, size_t _returnVariables);
	static SideEffects sideEffectsOfInstruction(evmasm::Instruction _instruction);

private:
	static Scopes constexpr instruction{1 << instructionBit};
	static Scopes constexpr replaced{1 << replacedInstructionBit};
	static Scopes constexpr objectAccess{1 << objectAccessBit};

	std::vector<std::tuple<Scopes, BuiltinFunctionForEVM>> m_scopesAndFunctions;
};

}
