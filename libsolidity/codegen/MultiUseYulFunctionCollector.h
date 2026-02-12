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
 * Container of (unparsed) Yul functions identified by name which are meant to be generated
 * only once.
 */

#pragma once

#include "fmt/format.h"
#include "liblangutil/Exceptions.h"
#include "libsolutil/Keccak256.h"


#include <concepts>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace solidity::frontend
{

/// @name toKeyString overloads
/// Customization point for converting key parts to strings for use in generated Yul function names.
/// Generic overloads are provided here. Type-specific overloads (e.g. for Type, EncodingOptions)
/// should be defined in the namespace of the respective type so they can be found via ADL.
/// @{

inline std::string toKeyString(std::string const& _s) { return _s; }
inline std::string toKeyString(std::string_view _s) { return std::string{_s}; }
inline std::string toKeyString(char const* _s) { return std::string{_s}; }
inline std::string toKeyString(bool _b) { return _b ? "true" : "false"; }

template<std::integral T> requires (!std::same_as<T, bool>)
std::string toKeyString(T _v) { return std::to_string(_v); }

template<typename T> requires std::is_enum_v<T>
std::string toKeyString(T _v) { return std::to_string(static_cast<std::underlying_type_t<T>>(_v)); }

template<typename T>
std::string toKeyString(std::reference_wrapper<T> _ref) { return toKeyString(_ref.get()); }

template<typename T>
std::string toKeyString(std::optional<T> const& _opt) { return _opt.has_value() ? toKeyString(*_opt) : "none"; }

struct HexKeyString {
	std::string value;
	friend std::string toKeyString(HexKeyString const& _hexKeyString) {
		return util::toHex(util::keccak256(_hexKeyString.value).asBytes());
	}
};

/// @}

template<typename F, typename Caller, typename... KeyParts>
concept FunctionCreatorConcept =
	std::invocable<F, Caller const*, std::string const&, KeyParts const&...> &&  // can be invoked with (Caller*, base name, key parts)
	requires(F f) { +f; };  // can be converted into c function pointer, ie, captureless

namespace detail
{
// Function creator concept with `int` as caller
static_assert(FunctionCreatorConcept<decltype([](int const*, std::string const&){}), int>);
static void test()
{
	int x = 5;
	// Function creator concept with `int` as caller but with capture
	static_assert(!FunctionCreatorConcept<decltype([&](int const*, std::string const&){std::ignore = x;}), int>);
}
}

/**
 * Container of (unparsed) Yul functions identified by name which are meant to be generated
 * only once.
 */
class MultiUseYulFunctionCollector
{
public:
	template<typename... KeyParts>
	std::string buildName(std::string_view const _base, std::tuple<KeyParts...> _keyParts)
	{
		solAssert(_base.find('#') == std::string_view::npos, "Base name must not contain '#'.");
		std::string name{_base};
		std::apply([&](auto const&... parts) {
			((name += [&]{
				auto s = toKeyString(parts);
				solAssert(s.find('#') == std::string::npos, "Key part must not contain '#'.");
				return "#" + s;
			}()), ...);
		}, _keyParts);
		return name;
	}

	/// Helper function that uses @a _creator to create a function and add it to
	/// @a m_requestedFunctions if it has not been created yet and returns the Yul function
	/// name in both cases.
	/// The deduplication key is built from @a _base and @a _keyParts using '#' as separator.
	/// The actual Yul function name is derived as base_N (with a per-base counter) to avoid
	/// any '#' characters in the emitted code.
	/// If the creator has arguments, they have to become part of _keyParts which leads to them being
	/// added as arguments to the creator.
	template<typename Caller, typename... KeyParts, FunctionCreatorConcept<Caller, KeyParts...> Creator>
	std::string createFunction(
		Caller const* _caller,
		std::string_view _base,
		std::tuple<KeyParts...> _keyParts,
		Creator _creator
	)
	{
		std::string key = buildName(_base, _keyParts);
		if (!m_requestedFunctions.contains(key))
		{
			m_requestedFunctions.insert(key);

			// Assign a clean Yul-valid function name.
			// The #-separated key is only used for deduplication in m_requestedFunctions.
			std::string yulName = assignYulName(_base, key);

#ifndef NDEBUG
			{
				std::vector<std::string> keyStrings;
				std::apply([&](auto const&... parts) {
					(keyStrings.push_back(toKeyString(parts)), ...);
				}, _keyParts);
				m_functionKeyDebugInfo[key] = {typeid(std::tuple<KeyParts...>).hash_code(), std::move(keyStrings)};
			}
#endif
			auto const function = std::apply(
				[&](auto const&... parts) { return _creator(_caller, yulName, parts...); },
				_keyParts
			);
			solAssert(!function.empty(), "");
			solAssert(function.find("function " + yulName + "(") != std::string::npos, "Function not properly named.");
			m_code += function;
		}
#ifndef NDEBUG
		else
		{
			auto it = m_functionKeyDebugInfo.find(key);
			if (it != m_functionKeyDebugInfo.end())
			{
				std::vector<std::string> keyStrings;
				std::apply([&](auto const&... parts) {
					(keyStrings.push_back(toKeyString(parts)), ...);
				}, _keyParts);
				solAssert(
					it->second.typeHash == typeid(std::tuple<KeyParts...>).hash_code() &&
					it->second.keyStrings == keyStrings,
					"Function name collision detected for: " + key
				);
			}
		}
#endif
		return m_keyToYulName.at(key);
	}


	template<typename Caller, FunctionCreatorConcept<Caller> Creator>
	std::string createFunction(
		Caller const* _caller,
		std::string_view _base,
		Creator&& _creator
	) {
		return createFunction(_caller, _base, std::tuple{}, std::forward<Creator>(_creator));
	}

	std::string createFunction(
		std::string const& _name,
		std::function<std::string(std::vector<std::string>&, std::vector<std::string>&)> const& _creator
	);

	/// @returns concatenation of all generated functions in the order in which they were
	/// generated.
	/// Clears the internal list, i.e. calling it again will result in an
	/// empty return value.
	std::string requestedFunctions();

	/// @returns true IFF a function with the specified name has already been collected.
	/// @a _name can be either a deduplication key (with '#') or a plain Yul name.
	bool contains(std::string const& _name) const { return m_requestedFunctions.contains(_name); }

private:
	/// Assigns a clean Yul-valid function name for the given deduplication key.
	/// If the key contains '#' (i.e. has key parts), generates base_N using a per-base counter.
	/// Otherwise uses the key directly as the Yul name.
	std::string assignYulName(std::string_view _base, std::string const& _key)
	{
		std::string yulName;
		if (_key.find('#') != std::string::npos)
			yulName = std::string(_base) + "_" + std::to_string(m_baseCounters[std::string(_base)]++);
		else
			yulName = _key;
		solAssert(
			m_yulNames.insert(yulName).second,
			"Generated Yul function name collides with existing name: " + yulName
		);
		m_keyToYulName[_key] = yulName;
		return yulName;
	}

	/// Deduplication keys (both #-separated and plain names).
	std::set<std::string> m_requestedFunctions;
	std::string m_code;
	/// Maps deduplication keys to their assigned Yul function names.
	std::map<std::string, std::string> m_keyToYulName;
	/// Per-base counters for generating unique Yul names (base_0, base_1, ...).
	std::map<std::string, size_t> m_baseCounters;
	/// All assigned Yul function names, for collision detection.
	std::set<std::string> m_yulNames;

#ifndef NDEBUG
	struct FunctionKeyInfo
	{
		size_t typeHash;
		std::vector<std::string> keyStrings;
	};
	std::map<std::string, FunctionKeyInfo> m_functionKeyDebugInfo;
#endif
};

}
