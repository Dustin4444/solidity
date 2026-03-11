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
/**
 * Unit tests for MultiUseYulFunctionCollector.
 */

#include <libsolidity/codegen/MultiUseYulFunctionCollector.h>

#include <boost/test/unit_test.hpp>

namespace solidity::frontend::test
{

namespace
{

/// Dummy caller type for the new-style createFunction overload.
struct DummyCaller {};

/// Helper: generates a minimal valid Yul function definition with given name and body expression.
std::string yulFunction(std::string const& _name, std::string const& _body = "")
{
	return "function " + _name + "() { " + _body + " }\n";
}

}

BOOST_AUTO_TEST_SUITE(MultiUseYulFunctionCollectorTest)

// --- toKeyString tests ---

BOOST_AUTO_TEST_CASE(toKeyString_string)
{
	BOOST_CHECK_EQUAL(toKeyString(std::string("hello")), "hello");
}

BOOST_AUTO_TEST_CASE(toKeyString_string_view)
{
	BOOST_CHECK_EQUAL(toKeyString(std::string_view("world")), "world");
}

BOOST_AUTO_TEST_CASE(toKeyString_cstring)
{
	BOOST_CHECK_EQUAL(toKeyString("literal"), "literal");
}

BOOST_AUTO_TEST_CASE(toKeyString_bool)
{
	BOOST_CHECK_EQUAL(toKeyString(true), "true");
	BOOST_CHECK_EQUAL(toKeyString(false), "false");
}

BOOST_AUTO_TEST_CASE(toKeyString_integral)
{
	BOOST_CHECK_EQUAL(toKeyString(42), "42");
	BOOST_CHECK_EQUAL(toKeyString(-1), "-1");
	BOOST_CHECK_EQUAL(toKeyString(static_cast<size_t>(100)), "100");
}

BOOST_AUTO_TEST_CASE(toKeyString_enum)
{
	enum class Color { Red = 0, Green = 1, Blue = 2 };
	BOOST_CHECK_EQUAL(toKeyString(Color::Red), "0");
	BOOST_CHECK_EQUAL(toKeyString(Color::Blue), "2");
}

BOOST_AUTO_TEST_CASE(toKeyString_reference_wrapper)
{
	std::string s = "wrapped";
	BOOST_CHECK_EQUAL(toKeyString(std::cref(s)), "wrapped");
}

BOOST_AUTO_TEST_CASE(toKeyString_optional)
{
	std::optional<int> empty;
	std::optional<int> full = 7;
	BOOST_CHECK_EQUAL(toKeyString(empty), "none");
	BOOST_CHECK_EQUAL(toKeyString(full), "7");
}

// --- buildKey tests ---

BOOST_AUTO_TEST_CASE(buildKey_base_only)
{
	MultiUseYulFunctionCollector collector;
	FunctionKey key = collector.buildKey("myFunc", std::tuple{});
	BOOST_REQUIRE_EQUAL(key.size(), 1);
	BOOST_CHECK(std::get<std::string>(key[0]) == "myFunc");
}

BOOST_AUTO_TEST_CASE(buildKey_with_string_parts)
{
	MultiUseYulFunctionCollector collector;
	FunctionKey key = collector.buildKey("encode", std::make_tuple(std::string("uint256"), true));
	BOOST_REQUIRE_EQUAL(key.size(), 3);
	BOOST_CHECK(std::get<std::string>(key[0]) == "encode");
	BOOST_CHECK(std::get<std::string>(key[1]) == "uint256");
	BOOST_CHECK(std::get<std::string>(key[2]) == "true");
}

BOOST_AUTO_TEST_CASE(buildKey_different_parts_different_keys)
{
	MultiUseYulFunctionCollector collector;
	FunctionKey k1 = collector.buildKey("f", std::make_tuple(1, 2));
	FunctionKey k2 = collector.buildKey("f", std::make_tuple(1, 3));
	FunctionKey k3 = collector.buildKey("f", std::make_tuple(2, 2));
	BOOST_CHECK(k1 != k2);
	BOOST_CHECK(k1 != k3);
	BOOST_CHECK(k2 != k3);
}

BOOST_AUTO_TEST_CASE(buildKey_same_parts_same_key)
{
	MultiUseYulFunctionCollector collector;
	FunctionKey k1 = collector.buildKey("f", std::make_tuple(42, std::string("x")));
	FunctionKey k2 = collector.buildKey("f", std::make_tuple(42, std::string("x")));
	BOOST_CHECK(k1 == k2);
}

BOOST_AUTO_TEST_CASE(buildKey_different_base_different_key)
{
	MultiUseYulFunctionCollector collector;
	FunctionKey k1 = collector.buildKey("foo", std::make_tuple(1));
	FunctionKey k2 = collector.buildKey("bar", std::make_tuple(1));
	BOOST_CHECK(k1 != k2);
}

// --- Structured key unambiguity ---
// Ensures that the variant-based key design prevents collisions that
// a separator-based string concatenation would miss.

BOOST_AUTO_TEST_CASE(buildKey_element_boundary_matters)
{
	// ("ab", "c") and ("a", "bc") must produce different keys
	// even though concatenation would yield the same string "abc".
	MultiUseYulFunctionCollector collector;
	FunctionKey k1 = collector.buildKey("base", std::make_tuple(std::string("ab"), std::string("c")));
	FunctionKey k2 = collector.buildKey("base", std::make_tuple(std::string("a"), std::string("bc")));
	BOOST_CHECK(k1 != k2);
}

BOOST_AUTO_TEST_CASE(buildKey_vector_vs_string_element)
{
	// A vector<string> element must not compare equal to a plain string element,
	// even if they contain the same data.
	FunctionKeyElement elem_str = std::string("only");
	FunctionKeyElement elem_vec = std::vector<std::string>{"only"};
	BOOST_CHECK(elem_str != elem_vec);
}

// --- New-style createFunction (with caller, base, keyParts, creator) ---

BOOST_AUTO_TEST_CASE(new_style_basic_creation)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	std::string name = collector.createFunction(
		&caller,
		"identity",
		std::tuple{},
		[](DummyCaller const*, std::string const& _name) {
			return yulFunction(_name);
		}
	);
	BOOST_CHECK(!name.empty());
	BOOST_CHECK(name.find("identity") != std::string::npos);
	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("function " + name + "(") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(new_style_deduplication)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	auto creator = [](DummyCaller const*, std::string const& _name, int) {
		return yulFunction(_name);
	};

	std::string name1 = collector.createFunction(&caller, "f", std::make_tuple(42), creator);
	std::string name2 = collector.createFunction(&caller, "f", std::make_tuple(42), creator);
	BOOST_CHECK_EQUAL(name1, name2);

	// The function should appear only once in the output.
	std::string code = collector.requestedFunctions();
	size_t first = code.find("function " + name1 + "(");
	size_t second = code.find("function " + name1 + "(", first + 1);
	BOOST_CHECK(first != std::string::npos);
	BOOST_CHECK(second == std::string::npos);
}

BOOST_AUTO_TEST_CASE(new_style_different_keys_different_functions)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	auto creator = [](DummyCaller const*, std::string const& _name, int _bits) {
		return yulFunction(_name, "let x := " + std::to_string(_bits));
	};

	std::string name1 = collector.createFunction(&caller, "shift", std::make_tuple(8), creator);
	std::string name2 = collector.createFunction(&caller, "shift", std::make_tuple(16), creator);
	BOOST_CHECK(name1 != name2);

	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("function " + name1 + "(") != std::string::npos);
	BOOST_CHECK(code.find("function " + name2 + "(") != std::string::npos);
	BOOST_CHECK(code.find("let x := 8") != std::string::npos);
	BOOST_CHECK(code.find("let x := 16") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(new_style_key_parts_passed_to_creator)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	std::string name = collector.createFunction(
		&caller,
		"f",
		std::make_tuple(std::string("hello"), 99),
		[](DummyCaller const*, std::string const& _name, std::string const& _s, int _n) {
			return yulFunction(_name, "// " + _s + " " + std::to_string(_n));
		}
	);
	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("// hello 99") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(new_style_no_key_parts_convenience)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	std::string name = collector.createFunction(
		&caller,
		"simple",
		[](DummyCaller const*, std::string const& _name) {
			return yulFunction(_name, "// simple body");
		}
	);
	BOOST_CHECK(!name.empty());
	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("// simple body") != std::string::npos);
}

// --- Old-style createFunction(name, lambda) ---

BOOST_AUTO_TEST_CASE(old_style_basic_creation)
{
	MultiUseYulFunctionCollector collector;
	std::string name = collector.createFunction(
		"my_func",
		std::function<std::string()>([&]() {
			return yulFunction("my_func", "// old style");
		})
	);
	BOOST_CHECK_EQUAL(name, "my_func");
	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("function my_func(") != std::string::npos);
	BOOST_CHECK(code.find("// old style") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(old_style_deduplication)
{
	MultiUseYulFunctionCollector collector;
	int callCount = 0;
	auto creator = std::function<std::string()>([&]() {
		callCount++;
		return yulFunction("dedup");
	});

	std::string name1 = collector.createFunction("dedup", creator);
	std::string name2 = collector.createFunction("dedup", creator);
	BOOST_CHECK_EQUAL(name1, "dedup");
	BOOST_CHECK_EQUAL(name2, "dedup");
	BOOST_CHECK_EQUAL(callCount, 1);
}

// --- Old-style createFunction(name, args/returns creator) ---

BOOST_AUTO_TEST_CASE(old_style_args_returns)
{
	MultiUseYulFunctionCollector collector;
	std::string name = collector.createFunction(
		"with_args",
		std::function<std::string(std::vector<std::string>&, std::vector<std::string>&)>(
			[](std::vector<std::string>& _args, std::vector<std::string>& _rets) -> std::string {
				_args = {"a", "b"};
				_rets = {"r"};
				return "let r := add(a, b)";
			}
		)
	);
	BOOST_CHECK_EQUAL(name, "with_args");
	std::string code = collector.requestedFunctions();
	BOOST_CHECK(code.find("function with_args(a, b) -> r") != std::string::npos);
}

// --- contains() ---

BOOST_AUTO_TEST_CASE(contains_after_old_style)
{
	MultiUseYulFunctionCollector collector;
	BOOST_CHECK(!collector.contains("foo"));
	collector.createFunction(
		"foo",
		std::function<std::string()>([]() { return yulFunction("foo"); })
	);
	BOOST_CHECK(collector.contains("foo"));
}

// --- requestedFunctions() clears state ---

BOOST_AUTO_TEST_CASE(requestedFunctions_clears)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;
	collector.createFunction(
		&caller,
		"f",
		[](DummyCaller const*, std::string const& _name) { return yulFunction(_name); }
	);
	collector.createFunction(
		"g",
		std::function<std::string()>([]() { return yulFunction("g"); })
	);

	std::string code1 = collector.requestedFunctions();
	BOOST_CHECK(!code1.empty());

	// After clear, should be empty and names should be available again.
	std::string code2 = collector.requestedFunctions();
	BOOST_CHECK(code2.empty());
}

BOOST_AUTO_TEST_CASE(requestedFunctions_reuse_after_clear)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	collector.createFunction(
		&caller,
		"f",
		std::make_tuple(1),
		[](DummyCaller const*, std::string const& _name, int) { return yulFunction(_name); }
	);
	std::string code1 = collector.requestedFunctions();

	// After clearing, the same key should create a new function (not be deduplicated).
	collector.createFunction(
		&caller,
		"f",
		std::make_tuple(1),
		[](DummyCaller const*, std::string const& _name, int) { return yulFunction(_name, "// second round"); }
	);
	std::string code2 = collector.requestedFunctions();
	BOOST_CHECK(code2.find("// second round") != std::string::npos);
}

// --- Cross-style Yul name collision detection ---

BOOST_AUTO_TEST_CASE(cross_style_yul_name_collision)
{
	// If old-style registers a name that matches what new-style would generate,
	// the m_yulNames set should catch the collision.
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	// Old-style: register "shift__0_8" as a Yul name (matching the pattern base__N_keyparts).
	collector.createFunction(
		"shift__0_8",
		std::function<std::string()>([]() { return yulFunction("shift__0_8"); })
	);

	// New-style with base "shift" and key part "8" would try to generate "shift__0_8" which is taken.
	BOOST_CHECK_THROW(
		collector.createFunction(
			&caller,
			"shift",
			std::make_tuple(std::string("8")),
			[](DummyCaller const*, std::string const& _name, std::string const&) { return yulFunction(_name); }
		),
		langutil::InternalCompilerError
	);
}

BOOST_AUTO_TEST_CASE(no_base_name_prefix_ambiguity)
{
	// base "foo" and base "foo_0" must not produce colliding Yul names.
	// "foo" → "foo__0_...", "foo_0" → "foo_0__0_..." — no conflict.
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	std::string n1 = collector.createFunction(
		&caller, "foo", std::make_tuple(std::string("x")),
		[](DummyCaller const*, std::string const& _name, std::string const&) { return yulFunction(_name); }
	);
	std::string n2 = collector.createFunction(
		&caller, "foo_0", std::make_tuple(std::string("x")),
		[](DummyCaller const*, std::string const& _name, std::string const&) { return yulFunction(_name); }
	);

	BOOST_CHECK_EQUAL(n1, "foo__0_x");
	BOOST_CHECK_EQUAL(n2, "foo_0__0_x");
	BOOST_CHECK(n1 != n2);
}

// --- Ordering: functions appear in generation order ---

BOOST_AUTO_TEST_CASE(generation_order)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	collector.createFunction(
		&caller,
		"first",
		[](DummyCaller const*, std::string const& _name) { return yulFunction(_name); }
	);
	collector.createFunction(
		&caller,
		"second",
		[](DummyCaller const*, std::string const& _name) { return yulFunction(_name); }
	);
	collector.createFunction(
		"third",
		std::function<std::string()>([]() { return yulFunction("third"); })
	);

	std::string code = collector.requestedFunctions();
	size_t pos1 = code.find("first");
	size_t pos2 = code.find("second");
	size_t pos3 = code.find("third");
	BOOST_CHECK(pos1 < pos2);
	BOOST_CHECK(pos2 < pos3);
}

// --- Yul name format: base_N ---

BOOST_AUTO_TEST_CASE(yul_name_includes_key_parts)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	std::string n0 = collector.createFunction(
		&caller, "abi_encode", std::make_tuple(std::string("uint256"), true),
		[](DummyCaller const*, std::string const& _name, std::string const&, bool) { return yulFunction(_name); }
	);
	std::string n1 = collector.createFunction(
		&caller, "abi_encode", std::make_tuple(std::string("int128"), false),
		[](DummyCaller const*, std::string const& _name, std::string const&, bool) { return yulFunction(_name); }
	);

	BOOST_CHECK_EQUAL(n0, "abi_encode__0_uint256_true");
	BOOST_CHECK_EQUAL(n1, "abi_encode__1_int128_false");
}

// --- Empty function body rejected ---

BOOST_AUTO_TEST_CASE(empty_body_rejected)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	BOOST_CHECK_THROW(
		collector.createFunction(
			&caller,
			"bad",
			[](DummyCaller const*, std::string const&) -> std::string { return ""; }
		),
		langutil::InternalCompilerError
	);
}

BOOST_AUTO_TEST_CASE(misnamed_function_rejected)
{
	MultiUseYulFunctionCollector collector;
	DummyCaller caller;

	BOOST_CHECK_THROW(
		collector.createFunction(
			&caller,
			"expected",
			[](DummyCaller const*, std::string const&) -> std::string {
				return "function wrong_name() { }\n";
			}
		),
		langutil::InternalCompilerError
	);
}

BOOST_AUTO_TEST_SUITE_END()

}
