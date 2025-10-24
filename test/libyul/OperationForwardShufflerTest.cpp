#include <libyul/backends/evm/ssa/OperationForwardShuffler.h>

#include <boost/test/unit_test.hpp>

#include <fmt/ranges.h>

#include <sstream>
#include <string>
#include <vector>

namespace
{
using Liveness = solidity::yul::ssa::LivenessAnalysis::LivenessData;
using Slot = solidity::yul::ssa::StackSlot;
using ValueId = solidity::yul::ssa::SSACFG::ValueId;

/// Parse a value ID token like "v172", "phi109", "lit7"
/// Returns std::nullopt for "JUNK"
std::optional<ValueId> parseValueToken(std::string const& token)
{
	if (token == "JUNK")
		return std::nullopt;

	if (token.starts_with("v"))
	{
		size_t num = std::stoull(token.substr(1));
		return ValueId::makeVariable(num);
	}

	if (token.starts_with("phi"))
	{
		size_t num = std::stoull(token.substr(3));
		return ValueId::makePhi(num);
	}

	if (token.starts_with("lit"))
	{
		size_t num = std::stoull(token.substr(3));
		return ValueId::makeLiteral(num);
	}
	throw std::runtime_error("Unknown token: " + token);
}

/// Parse a string like "[v172, phi109, lit7, JUNK]" into Stack::Data
std::vector<Slot> parseStackData(std::string_view _input)
{
	std::vector<Slot> result;
	std::string input(_input);

	// Remove whitespace
	input.erase(std::ranges::remove_if(input, ::isspace).begin(), input.end());

	// Remove brackets
	if (!input.empty() && input.front() == '[')
		input.erase(input.begin());
	if (!input.empty() && input.back() == ']')
		input.pop_back();

	// Split by comma
	std::stringstream ss(input);
	std::string token;

	while (std::getline(ss, token, ','))
	{
		if (token.empty())
			continue;

		if (auto valueId = parseValueToken(token))
			result.push_back(Slot::makeValueID(*valueId));
		else
			result.push_back(Slot::makeJunk());
	}

	return result;
}

/// Parse liveness like "[phi109, phi150, v172]"
/// Returns Liveness with reference count 1 for each value
Liveness parseLiveness(std::string_view _input)
{
	std::vector<std::pair<ValueId, uint32_t>> liveCounts;
	std::string input(_input);

	// Remove whitespace
	input.erase(std::ranges::remove_if(input, ::isspace).begin(), input.end());

	// Remove brackets
	if (!input.empty() && input.front() == '[')
		input.erase(input.begin());
	if (!input.empty() && input.back() == ']')
		input.pop_back();

	// Split by comma
	std::stringstream ss(input);
	std::string token;

	while (std::getline(ss, token, ','))
	{
		if (token.empty())
			continue;

		auto valueId = parseValueToken(token);
		if (valueId)
			liveCounts.emplace_back(*valueId, 1);  // Default reference count of 1
	}

	return {liveCounts.begin(), liveCounts.end()};
}

struct StackManipulationCallbacks
{
	void swap(size_t _depth) const
	{
		if (hook)
			(*hook)(fmt::format("SWAP{}", _depth));
	}
	void dup(size_t const _depth) const
	{
		if (hook)
			(*hook)(fmt::format("DUP{}", _depth));
	}
	void push(Slot const& _slot) const
	{
		if (hook)
			(*hook)(fmt::format("PUSH {}", slotToString(_slot)));
	}
	void pop() const
	{
		if (hook)
			(*hook)("POP");
	}

	std::optional<std::function<void(std::string const&)>> hook = std::nullopt;
};
using Stack = solidity::yul::ssa::Stack<StackManipulationCallbacks>;

class TraceRecorder {
	static constexpr size_t operationColumnWidth = 12;
	static constexpr size_t slotColumnWidth = 7;
	static constexpr char junkSymbol = '*';

public:
	TraceRecorder(std::ostream& _out, Stack::Data _targetArgs, Liveness _targetTail, size_t _targetStackSize):
		m_out(_out),
		m_targetArgs(std::move(_targetArgs)),
		m_targetTail(std::move(_targetTail)),
		m_targetStackSize(_targetStackSize),
		m_targetTailSize(
			[&] {
				yulAssert(_targetStackSize >= m_targetArgs.size());
				return _targetStackSize - m_targetArgs.size();
			}()
		)
	{}

	void record(std::string const& _operation, Stack::Data const& _stack)
	{
		m_entries.emplace_back(_operation, _stack);
	}

	~TraceRecorder()
	{
		if (m_entries.empty())
			return;

		size_t maxStackDepth = 0;
		for (const auto& [operation, stackAfter] : m_entries)
			maxStackDepth = std::max(maxStackDepth, stackAfter.size());

		if (maxStackDepth == 0)
			return;

		bool const hasExcess = maxStackDepth > m_targetStackSize;

		m_out << '\n';
		emitHeader(maxStackDepth, hasExcess);
		emitSeparatorLine(maxStackDepth, hasExcess);
		for (auto const& entry : m_entries)
			emitDataRow(entry, maxStackDepth, hasExcess);
		emitSeparatorLine(maxStackDepth, hasExcess);
		emitTargetRow(maxStackDepth, hasExcess);
	}

private:
	struct TraceEntry {
		std::string operation;
		Stack::Data stackAfter;
	};

	std::ostream& m_out;
	std::vector<TraceEntry> m_entries;
	Stack::Data const m_targetArgs;
	Liveness const m_targetTail;
	size_t const m_targetStackSize;
	size_t const m_targetTailSize;

	void emitSeparator(size_t const _index, bool const _hasExcess, char const _junction) const
	{
		if (_index == m_targetTailSize && !m_targetArgs.empty() && m_targetTailSize > 0)
			m_out << ' ' << _junction;
		else if (_hasExcess && _index == m_targetTailSize + m_targetArgs.size())
			m_out << ' ' << _junction;
	}

	void emitHeader(size_t const _maxStackDepth, bool const _hasExcess) const
	{
		m_out << fmt::format("{:>{}}", "", operationColumnWidth) << "|";
		for (size_t i = 0; i < _maxStackDepth; ++i)
		{
			emitSeparator(i, _hasExcess, '|');
			m_out << fmt::format("{:>{}}", i, slotColumnWidth);
		}
		m_out << "\n";
	}

	void emitSeparatorLine(size_t _maxStackDepth, bool const _hasExcess) const
	{
		m_out << fmt::format("{:>{}}", "", operationColumnWidth) << '+';
		for (size_t i = 0; i < _maxStackDepth; ++i)
		{
			emitSeparator(i, _hasExcess, '+');
			m_out << std::string(slotColumnWidth, '-');
		}
		m_out << '\n';
	}

	void emitDataRow(TraceEntry const& _entry, size_t _maxStackDepth, bool const _hasExcess) const
	{
		m_out << fmt::format("{:>{}}", _entry.operation, operationColumnWidth) << "|";
		for (size_t i = 0; i < _maxStackDepth; ++i)
		{
			emitSeparator(i, _hasExcess, '|');
			if (i < _entry.stackAfter.size())
			{
				auto const& slot = _entry.stackAfter[i];
				std::string slotStr = slot.isJunk()
					? std::string(1, junkSymbol)
					: solidity::yul::ssa::slotToString(slot);
				m_out << fmt::format("{:>{}}", slotStr, slotColumnWidth);
			}
			else
				m_out << std::string(slotColumnWidth, ' ');
		}
		m_out << '\n';
	}

	void emitTargetRow(size_t const _maxStackDepth, bool const _hasExcess) const
	{
		m_out << fmt::format("{:>{}}", "(target)", operationColumnWidth) << "|";

		// Print tail region with set notation
		if (m_targetTailSize > 0)
		{
			std::string tailSetStr;
			if (!m_targetTail.empty())
				tailSetStr = fmt::format(
				"{{{}}}",
					fmt::join(
						m_targetTail | ranges::views::keys | ranges::views::transform(
							[](auto const& id) { return solidity::yul::ssa::slotToString(Slot::makeValueID(id)); }
						),
					", ")
				);
			m_out << fmt::format("{:>{}}", tailSetStr, m_targetTailSize * slotColumnWidth);
		}

		// Args separator
		if (!m_targetArgs.empty() && m_targetTailSize > 0)
			m_out << " |";

		// Print args region
		for (auto const& slot : m_targetArgs)
		{
			std::string slotStr = slot.isJunk() ? std::string(1, junkSymbol) : solidity::yul::ssa::slotToString(slot);
			m_out << fmt::format("{:>{}}", slotStr, slotColumnWidth);
		}

		// Excess separator and region
		if (_hasExcess)
		{
			m_out << " |";
			size_t excessSize = _maxStackDepth - m_targetTailSize - m_targetArgs.size();
			m_out << std::string(excessSize * slotColumnWidth, ' ');
		}

		m_out << '\n';
	}
};
}

namespace solidity::yul::test
{
BOOST_AUTO_TEST_SUITE(OperationForwardShufflerTest)

BOOST_AUTO_TEST_CASE(TestCycle)
{
	Stack::Data data = parseStackData("[v64, JUNK, v64, JUNK, v60, v74, JUNK, v60]");
	Stack::Data args = parseStackData("[v74, lit15]");
	Liveness liveness = parseLiveness("[v60, v64]");

	Stack stack(data, {});
	ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 7, false);
}

BOOST_AUTO_TEST_CASE(TestJunk)
{
	{
		/*// todo extremely inefficient atm
		Stack::Data data = parseStackData("[JUNK, JUNK, v56, v57, JUNK, JUNK]");
		Stack::Data args = parseStackData("[JUNK, JUNK, v56, v57, lit0, v56, lit11]");
		Liveness liveness = parseLiveness("");

		Stack stack(data, {.hook = [&]{ std::cout << " -> " << ssa::stackToString(data) << std::endl; }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, args.size(), false);*/
	}
	/*{
		Stack::Data data = parseStackData("[JUNK, v44, v129, v43, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v101, JUNK, v130, v131]");
		Stack::Data args = parseStackData("[JUNK, v44, JUNK, v43, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v101, JUNK, v130, v131, lit0, v129]");
		Liveness liveness = parseLiveness("");

		Stack stack(data, {.hook = [&]{ std::cout << " -> " << ssa::stackToString(data) << std::endl; }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, args.size(), false);
	}*/
	/*{
		Stack::Data data = parseStackData("[JUNK, v189, phi112, JUNK, v204, JUNK, JUNK, JUNK, JUNK, JUNK, v185, JUNK, v188, v190, v191, JUNK, v199, phi112, phi112, v205, v206]");
		Stack::Data args = parseStackData("[JUNK, v199, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v185, JUNK, v188, v190, v191, JUNK, v189, JUNK, v206, v205, v204]");
		Liveness liveness;

		Stack stack(data, {});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, args.size(), false);
	}*/
	/*{
		Stack::Data data = parseStackData("[JUNK, v189, phi112, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v185, phi115, v188, v190, v191, phi113, v199, phi112, v199, v202]");
		Stack::Data args = parseStackData("[v202]");
		Liveness liveness = parseLiveness("[phi112, phi113, phi115, v185, v188, v189, v190, v191, v199]");

		Stack stack(data, {.hook = [&]{ std::cout << " -> " << ssa::stackToString(data) << std::endl; }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 19, false);
	}*/
	/*{
		TraceRecorder trace;
		Stack::Data data = parseStackData("[JUNK, v12, phi9, phi13, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v65, v67]");
		Stack::Data args = parseStackData("[lit27, phi13, phi9, v12, v67]");
		Liveness liveness = parseLiveness("[phi9, v12, phi13, v65]");

		trace.entries.push_back({"(initial)", data});
		Stack stack(data, {.hook = [&](std::string const& op){ std::cout << op << ", "; trace.entries.push_back({op, data}); }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 21, false);
	}*/
	/*{
		TraceRecorder trace;
		Stack::Data data = parseStackData("[JUNK, JUNK, JUNK, JUNK, JUNK, v179, JUNK, phi233, phi234, phi236, v184, v185, JUNK, phi239, phi240, phi245, v188, JUNK, v190, v193, v194, phi253, JUNK, phi255, phi256, phi257, v197]");
		Stack::Data args = parseStackData("[lit35, v197, phi255, phi257]");
		Liveness liveness = parseLiveness("[v179, v184, v185, v188, v190, v193, v194, phi233, phi234, phi236, phi239, phi240, phi245, phi253, phi255, phi256]");

		trace.entries.push_back({"(initial)", data});
		Stack stack(data, {.hook = [&](std::string const& op){ std::cout << op << ", "; trace.entries.push_back({op, data}); }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 25, false);
	}*/
	/*{
		TraceRecorder trace;
		Stack::Data data = parseStackData("[v2, v3, v4, v5, v6, v7, v8, v9, v10]");
		Stack::Data args = parseStackData("[lit3, v9, v8, v7, v6, v5, v4, v3, v2, v10]");
		Liveness liveness = parseLiveness("[v10]");

		trace.entries.push_back({"(initial)", data});
		Stack stack(data, {.hook = [&](std::string const& op){ std::cout << op << ", " << std::flush; trace.entries.push_back({op, data}); }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 17, false);
	}*/
	/*{
		// todo inefficient sequence of swap1 + pop
		TraceRecorder trace;
		Stack::Data data = parseStackData("[JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v55, v71, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v94]");
		Stack::Data args = parseStackData("[lit11, v55]");
		Liveness liveness = parseLiveness("[v55, v71, v94]");

		trace.entries.push_back({"(initial)", data});
		Stack stack(data, {.hook = [&](std::string const& op){ std::cout << op << ", " << std::flush; trace.entries.push_back({op, data}); }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 70, false);
	}*/
	{
		Stack::Data data = parseStackData("[JUNK, JUNK, JUNK, JUNK, JUNK, v179, JUNK, phi233, phi234, phi236, v184, v185, JUNK, phi239, phi240, phi245, v188, JUNK, v190, v193, v194, phi253, JUNK, phi255, phi256, phi257, v197]");
		Stack::Data args = parseStackData("[lit36, v197, phi255, phi257]");
		Liveness liveness = parseLiveness("[v179, v184, v185, v188, v190, v193, v194, phi233, phi234, phi236, phi239, phi240, phi245, phi253, phi255, phi256]");
		size_t const targetStackSize = 25;

		TraceRecorder trace(std::cout, args, liveness, targetStackSize);
		trace.record("(initial)", data);
		Stack stack(data, {.hook = [&](std::string const& op){ trace.record(op, data); }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, targetStackSize, false);
	}
	/*{
		Stack::Data data = parseStackData("[JUNK, v12, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, v68, JUNK, phi111, v84, v86]");
		Stack::Data args = parseStackData("[v12, v86]");
		Liveness liveness = parseLiveness("[v68, v84, phi111]");

		Stack stack(data, {.hook = [&]{ std::cout << " -> " << ssa::stackToString(data) << std::endl; }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 20, false);
	}*/
	/*{
		// this ends in a cycle but also it does this swap15 which i dont think is great
		// instead we should swap up closer to the top if possible so the top region stays intact for the most part
		// (reduces entropy)
		Stack::Data data = parseStackData("[v6, v5, v4, v3, v2, JUNK, JUNK, v59, v60, JUNK, JUNK, JUNK, JUNK, v98, JUNK, JUNK, JUNK, v113, v114, v115, v116]");
		Stack::Data args = parseStackData("[v116, v5]");
		Liveness liveness = parseLiveness("[v2, v3, v4, v5, v6, v59, v60, v98, v113, v114, v115]");

		Stack stack(data, {.hook = [&]{ std::cout << " -> " << ssa::stackToString(data) << std::endl; }});
		ssa::OperationForwardShuffler<StackManipulationCallbacks>::shuffle(stack, args, liveness, 20, false);
	}*/
}

BOOST_AUTO_TEST_SUITE_END()
}
