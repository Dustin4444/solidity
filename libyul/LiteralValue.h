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
 * Value type of a Yul literal, decoupled from the AST node so that backends can carry literal
 * values without depending on the Yul syntax tree.
 */

#pragma once

#include <libsolutil/Numeric.h>

#include <memory>
#include <optional>
#include <string>

namespace solidity::yul
{

/// Literal number or string (up to 32 bytes)
enum class LiteralKind { Number, Boolean, String };
/// Literal value that holds a u256 word of data, can be of LiteralKind type and - in case of arguments to
/// builtins - exceed the u256 word (32 bytes), in which case the value is stored as string. The former is constructed
/// via u256 word and optional hint and leads to unlimited == false, the latter is
/// constructed via the string constructor and leads to unlimited == true.
class LiteralValue {
public:
	using Data = u256;
	using BuiltinStringLiteralData = std::string;
	using RepresentationHint = std::shared_ptr<std::string>;

	LiteralValue() = default;
	explicit LiteralValue(std::string _builtinStringLiteralValue);
	explicit LiteralValue(Data const& _data, std::optional<std::string> const& _hint = std::nullopt);

	bool operator==(LiteralValue const& _rhs) const;
	bool operator<(LiteralValue const& _rhs) const;
	Data const& value() const;
	BuiltinStringLiteralData const& builtinStringLiteralValue() const;
	bool unlimited() const;
	RepresentationHint const& hint() const;

private:
	std::optional<Data> m_numericValue;
	std::shared_ptr<std::string> m_stringValue;
};

}
