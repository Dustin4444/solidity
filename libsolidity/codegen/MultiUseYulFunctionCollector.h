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

#include <liblangutil/Exceptions.h>
#include <libsolutil/Keccak256.h>

#include <concepts>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>
#include <vector>

namespace solidity::frontend
{

/// Structured deduplication key for generated Yul functions.
/// Each element is either a single string or a vector of strings (for compound key parts
/// like TypePointers). Using std::variant ensures structural unambiguity: a single-string
/// element never compares equal to a vector element, and vector elements are compared
/// element-by-element without any separator-based concatenation.
using FunctionKeyElement = std::variant<std::string, std::vector<std::string>>;
using FunctionKey = std::vector<FunctionKeyElement>;

/// @name toKeyString overloads
/// Customization point for converting key parts to strings for use in FunctionKey elements.
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

/// @name toKeyElement overloads
/// Converts a key part into a FunctionKeyElement (string or vector of strings).
/// The default delegates to toKeyString(), returning a single std::string.
/// Overloads returning std::vector<std::string> produce compound key elements
/// (e.g. for TypePointers, where each type identifier is a separate element).
/// @{

template<typename T>
std::string toKeyElement(T const& _v) { return toKeyString(_v); }

template<typename T>
auto toKeyElement(std::reference_wrapper<T> _ref) { return toKeyElement(_ref.get()); }

/// @}

template<typename F, typename Caller, typename... KeyParts>
concept FunctionCreatorConcept =
	std::invocable<F, Caller const*, std::string const&, KeyParts const&...> &&  // can be invoked with (Caller*, function name, key parts)
	requires(F f) { +f; };  // can be converted into c function pointer, ie, captureless

/**
 * Container of (unparsed) Yul functions identified by name which are meant to be generated
 * only once.
 */
class MultiUseYulFunctionCollector
{
public:
	/// Builds a structured deduplication key from a base name and key parts.
	/// Each key part becomes a separate element in the vector, ensuring unambiguous comparison
	/// regardless of what characters appear inside individual key part strings.
	template<typename... KeyParts>
	FunctionKey buildKey(std::string_view _base, std::tuple<KeyParts...> const& _keyParts)
	{
		FunctionKey key;
		key.emplace_back(std::string{_base});
		std::apply([&](auto const&... parts) {
			(key.push_back(toKeyElement(parts)), ...);
		}, _keyParts);
		return key;
	}

	/// Helper function that uses @a _creator to create a function and add it to
	/// @a m_requestedFunctions if it has not been created yet and returns the Yul function
	/// name in both cases.
	/// The deduplication key is a vector built from @a _base and @a _keyParts.
	/// The actual Yul function name is derived as base_N (with a per-base counter).
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
		FunctionKey const key = buildKey(_base, _keyParts);
		if (!m_requestedFunctionKeys.contains(key))
		{
			m_requestedFunctionKeys.insert(key);

			// Assign a clean Yul-valid function name using a per-base counter.
			std::string yulName = assignYulName(_base, key);

#ifndef NDEBUG
			m_functionKeyDebugInfo[key] = typeid(std::tuple<KeyParts...>).hash_code();
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
				solAssert(
					it->second == typeid(std::tuple<KeyParts...>).hash_code(),
					"Function key collision: same key produced by different tuple types"
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

	/// Helper function that uses @a _creator to create a function and add it to
	/// the collected functions if it has not been created yet.
	/// Uses the name string directly as both the deduplication key and the Yul function name.
	/// Allows capturing lambdas. Suitable for AST-node-derived names with no collision risk.
	std::string createFunction(
		std::string const& _name,
		std::function<std::string()> const& _creator
	);

	/// @returns concatenation of all generated functions in the order in which they were
	/// generated.
	/// Clears the internal list, i.e. calling it again will result in an
	/// empty return value.
	std::string requestedFunctions();

	/// @returns true IFF a function with the specified name has already been collected.
	bool contains(std::string const& _name) const { return m_yulNames.contains(_name); }

private:
	/// Assigns a clean Yul-valid function name for the given deduplication key.
	/// Generates base__N_keypart1_keypart2_... using a per-base counter and flattened key parts.
	/// The double underscore between base and counter prevents ambiguity when
	/// one base name is a prefix of another (e.g. "base" vs "base_0").
	std::string assignYulName(std::string_view _base, FunctionKey const& _key)
	{
		std::string yulName = std::string(_base) + "__" + std::to_string(m_baseCounters[std::string(_base)]++);
		// Append flattened key parts (skip element 0 which is the base name).
		for (size_t i = 1; i < _key.size(); ++i)
			std::visit([&]<typename T>(T const& _elem) {
				if constexpr (std::is_same_v<T, std::string>)
					yulName += "_" + _elem;
				else
					for (auto const& s: _elem)
						yulName += "_" + s;
			}, _key[i]);
		solAssert(
			m_yulNames.insert(yulName).second,
			"Generated Yul function name collides with existing name: " + yulName
		);
		m_keyToYulName[_key] = yulName;
		return yulName;
	}

	/// Structured deduplication keys for all createFunction overloads.
	std::set<FunctionKey> m_requestedFunctionKeys;
	std::string m_code;
	/// Maps structured keys to their assigned Yul function names.
	std::map<FunctionKey, std::string> m_keyToYulName;
	/// Per-base counters for generating unique Yul names (base_0, base_1, ...).
	std::map<std::string, size_t> m_baseCounters;
	/// All assigned Yul function names (from both old and new style), for collision detection.
	std::set<std::string> m_yulNames;

#ifndef NDEBUG
	/// Maps keys to the typeid hash of the tuple that produced them, for collision detection.
	std::map<FunctionKey, size_t> m_functionKeyDebugInfo;
#endif
};

}
