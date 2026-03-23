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

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

#include <string>
#include <variant>

namespace solidity::yul::ssa::debug
{

inline std::string operationName(SSACFG::Operation const& _operation)
{
	return std::visit(util::GenericVisitor{
		[](SSACFG::Call const& _call) { return _call.function.get().name.str(); },
		[](SSACFG::BuiltinCall const& _call) { return _call.builtin.get().name; },
		[](SSACFG::LiteralAssignment const&) -> std::string { return "assign"; }
	}, _operation.kind);
}

struct StackLayoutGenerationFlags
{
	consteval StackLayoutGenerationFlags(bool _enabled, bool _shuffler):
		enabled(_enabled),
		shuffler(_enabled && _shuffler)
	{}

	bool enabled;
	bool shuffler;
};

struct CodeTransformFlags
{
	consteval CodeTransformFlags(bool _enabled, bool _dotOutput, bool _shuffler):
		enabled(_enabled),
		dotOutput(_enabled && _dotOutput),
		shuffler(_enabled && _shuffler)
	{}

	bool enabled;
	bool dotOutput;
	bool shuffler;
};

inline constexpr StackLayoutGenerationFlags stackLayoutGeneration(false, true);
inline constexpr CodeTransformFlags codeTransform(false, true, true);

}
