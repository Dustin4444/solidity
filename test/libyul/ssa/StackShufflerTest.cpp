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

#include <test/libyul/ssa/StackShufflerTest.h>

#include <libyul/backends/evm/ssa/LivenessAnalysis.h>
#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackShuffler.h>

#include <range/v3/view/split.hpp>

#include <fmt/ranges.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::test;
using namespace solidity::yul::test::ssa;

namespace
{
std::string_view constexpr parserKeyInitialStack {"initial"};
std::string_view constexpr parserKeyStackTop {"targetStackTop"};
std::string_view constexpr parserKeyTailSet {"targetStackTailSet"};
std::string_view constexpr parserKeyStackSize {"targetStackSize"};
std::string_view constexpr parserKeyAllowSpilling {"allowSpilling"};
std::string_view constexpr parserKeyInitialSpilled {"initialSpilledSet"};

using Liveness = LivenessAnalysis::LivenessData;
using Slot = StackSlot;
using ValueId = SSACFG::ValueId;
struct StackManipulationCallbacks
{
	void swap(StackDepth _depth) const
	{
		if (hook)
			(*hook)(fmt::format("SWAP{}", _depth.value));
	}
	void dup(StackDepth const _depth) const
	{
		if (hook)
			(*hook)(fmt::format("DUP{}", _depth.value));
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
using TestStack = Stack<StackManipulationCallbacks>;

/// removes leading and trailing whitespace from a string view
std::string_view trim(std::string_view s)
{
	s.remove_prefix(std::min(s.find_first_not_of(" \t\r\v\n"), s.size()));
	s.remove_suffix(std::min(s.size() - s.find_last_not_of(" \t\r\v\n") - 1, s.size()));
	return s;
}

/// Parse a value ID token like "v172", "phi109", "lit7", or "JUNK".
Slot parseSlot(std::string_view token)
{
	if (token == "JUNK")
		return Slot::makeJunk();

	if (token.starts_with("v"))
	{
		if (auto const num = util::parseArithmetic<ValueId::ValueType>(token.substr(1)))
			return Slot::makeValueID(ValueId::makeOutput(SSACFG::InstId{*num}, 0, SSACFG::Opcode::BuiltinCall));
		throw std::runtime_error(fmt::format("Couldn't parse variable token: {}", token));
	}

	if (token.starts_with("phi"))
	{
		if (auto const num = util::parseArithmetic<ValueId::ValueType>(token.substr(3)))
			return Slot::makeValueID(ValueId::makeOutput(SSACFG::InstId{*num}, 0, SSACFG::Opcode::Phi));
		throw std::runtime_error(fmt::format("Couldn't parse phi token: {}", token));
	}

	if (token.starts_with("lit"))
	{
		if (auto const num = util::parseArithmetic<ValueId::ValueType>(token.substr(3)))
			return Slot::makeValueID(ValueId::makeOutput(SSACFG::InstId{*num}, 0, SSACFG::Opcode::Const));
		throw std::runtime_error(fmt::format("Couldn't parse literal token: {}", token));
	}

	static constexpr std::string_view returnLabelPrefix = "ReturnLabel[";
	if (token.starts_with(returnLabelPrefix) && token.ends_with("]"))
	{
		auto const inner = token.substr(returnLabelPrefix.size(), token.size() - returnLabelPrefix.size() - 1);
		if (auto const num = util::parseArithmetic<ControlFlowGraphs::FunctionGraphID>(inner))
			return Slot::makeFunctionReturnLabel(*num);
		throw std::runtime_error(fmt::format("Couldn't parse ReturnLabel token: {}", token));
	}

	throw std::runtime_error(fmt::format("Unknown token: {}", token));
}

/// Parse a string like "[v172, phi109, lit7, JUNK]" into Stack::Data
TestStack::Data parseSlots(std::string_view _input, char const brackBegin = '[', char const brackEnd = ']')
{
	TestStack::Data result;

	// trim and remove brackets
	{
		_input = trim(_input);
		yulAssert(_input.starts_with(brackBegin));
		_input.remove_prefix(1);
		yulAssert(_input.ends_with(brackEnd));
		_input.remove_suffix(1);
	}

	for (auto&& slotToken: ranges::views::split(_input, ','))
	{
		auto const slotTokenBegin = ranges::begin(slotToken);
		auto const slotTokenEnd  = ranges::end(slotToken);

		std::string_view token;
		if(slotTokenBegin != slotTokenEnd)
			token = {&*slotTokenBegin, static_cast<std::size_t>(ranges::distance(slotTokenBegin, slotTokenEnd))};
		token = trim(token);
		yulAssert(!token.empty(), "Empty token.");
		result.push_back(parseSlot(token));
	}
	return result;
}

/// Parse liveness like "{phi109, phi150, v172}"
/// Returns Liveness with reference count 1 for each value
Liveness parseLiveness(std::string_view _input)
{
	auto const slots = parseSlots(_input, '{', '}');
	std::vector<std::pair<ValueId, uint32_t>> liveCounts;
	liveCounts.reserve(slots.size());
	for (auto const& slot: slots)
	{
		yulAssert(slot.isValueID(), "Only value IDs are permitted in liveness definition.");
		liveCounts.emplace_back(slot.valueID(), 1);
	}
	return {liveCounts.begin(), liveCounts.end()};
}

struct ShuffleTestInput
{
	std::optional<TestStack::Data> initial;
	std::optional<TestStack::Data> targetStackTop;
	Liveness targetStackTailSet{};
	std::optional<size_t> targetStackSize;
	bool allowSpilling = false;
	SpilledVariables initialSpilledSet{};

	bool valid() const
	{
		bool const fullySpecified =
			initial.has_value() &&
			targetStackTop.has_value() &&
			targetStackSize.has_value();
		bool const exactMode =
			initial.has_value() &&
			targetStackTop.has_value() &&
			!targetStackSize.has_value();
		return fullySpecified || exactMode;
	}

	static ShuffleTestInput parse(std::string_view _source)
	{
		ShuffleTestInput result;

		auto const stripComment = [](std::string_view sv) -> std::string_view
		{
			auto const pos = sv.find("//");
			if (pos != std::string_view::npos)
				return sv.substr(0, pos);
			return sv;
		};

		for (auto&& lineRange: ranges::views::split(_source, '\n'))
		{
			auto lineBegin = ranges::begin(lineRange);
			auto lineEnd  = ranges::end(lineRange);
			if (lineBegin == lineEnd)
				continue;

			std::string_view line{&*lineBegin, static_cast<std::size_t>(ranges::distance(lineBegin, lineEnd))};
			line = trim(stripComment(line));
			if (line.empty())
				continue;

			auto const colonPos = line.find(':');
			if (colonPos == std::string_view::npos)
				continue;

			auto const key = trim(line.substr(0, colonPos));
			auto const value = trim(line.substr(colonPos + 1));

			if (key == parserKeyInitialStack)
				result.initial = parseSlots(value, '[', ']');
			else if (key == parserKeyStackTop)
				result.targetStackTop = parseSlots(value, '[', ']');
			else if (key == parserKeyTailSet)
				result.targetStackTailSet = parseLiveness(value);
			else if (key == parserKeyStackSize)
			{
				if (auto num = util::parseArithmetic<std::size_t>(value))
					result.targetStackSize = *num;
				else
					throw std::runtime_error(fmt::format("Couldn't parse targetStackSize: {}", value));
			}
			else if (key == parserKeyAllowSpilling)
			{
				if (value == "true")
					result.allowSpilling = true;
				else if (value == "false")
					result.allowSpilling = false;
				else
					throw std::runtime_error(fmt::format("Couldn't parse allowSpilling: {}", value));
			}
			else if (key == parserKeyInitialSpilled)
				for (auto const& [valueId, _]: parseLiveness(value))
					result.initialSpilledSet.spill(valueId);

		}

		if (result.valid() && !result.targetStackSize)
		{
			yulAssert(
				result.targetStackTailSet.empty(),
				"Can only infer target stack size if targetStackTailSet is empty / unset."
			);
			result.targetStackSize = result.targetStackTop->size();
		}
		return result;
	}
};

/// Records a shuffling trace and produces formatted output into some ostream when going out of scope
class TraceRecorder
{
	static size_t constexpr operationColumnWidth = 12;
	static size_t constexpr slotColumnWidth = 7;
	static char constexpr junkSymbol = '*';

public:
	TraceRecorder(
		std::ostream& _out,
		TestStack::Data const& _targetArgs,
		Liveness const& _targetTail,
		size_t _targetStackSize,
		SpilledVariables const& _spillSet
	):
		m_out(_out),
		m_targetArgs(_targetArgs),
		m_targetTail(_targetTail),
		m_spillSet(_spillSet),
		m_targetStackSize(_targetStackSize),
		m_targetTailSize(
			[&] {
				yulAssert(_targetStackSize >= m_targetArgs.size());
				return _targetStackSize - m_targetArgs.size();
			}()
		),
		m_tailSetStr(
			fmt::format(
				"{{{}}}",
				fmt::join(
					m_targetTail | ranges::views::keys | ranges::views::transform(
						[this](auto const& id) {
							std::string const suffix = m_spillSet.isSpilled(id) ? "*" : "";
							return slotToString(Slot::makeValueID(id)) + suffix;
						}
					),
					", "
				)
			)
		),
		// Width of the phantom "tail annotation" column, shown only when the set is non-empty
		// but the tail region has zero real columns (all tail-set members spilled or coinciding
		// with args)
		m_tailAnnotationWidth(
			m_targetTailSize == 0 && !m_targetTail.empty() ? m_tailSetStr.size() + 2 : 0
		)
	{}

	void record(std::string const& _operation, TestStack::Data const& _stack)
	{
		m_entries.push_back(TraceEntry{_operation, _stack});
	}

	void truncate(size_t const _maxEntries)
	{
		if (m_entries.size() > _maxEntries)
		{
			m_entries.resize(_maxEntries);
			m_truncated = true;
		}
	}

	~TraceRecorder()
	{
		if (m_entries.empty())
			return;

		size_t maxStackDepth = 0;
		for (const auto& [operation, stackAfter]: m_entries)
			maxStackDepth = std::max(maxStackDepth, stackAfter.size());

		if (maxStackDepth == 0)
			return;

		std::size_t const numColumns = std::max(maxStackDepth, m_targetStackSize);
		std::vector columnWidths(numColumns, slotColumnWidth);
		for (const auto& [operation, stackAfter]: m_entries)
			for (std::size_t i = 0; i < stackAfter.size(); ++i)
			{
				std::string const slotStr = stackAfter[i].isJunk() ? std::string(1, junkSymbol) : slotToString(stackAfter[i]);
				columnWidths[i] = std::max(columnWidths[i], slotStr.size() + 1);
			}
		for (std::size_t i = 0; i < m_targetArgs.size() && m_targetTailSize + i < numColumns; ++i)
		{
			std::string const slotStr = m_targetArgs[i].isJunk() ? std::string(1, junkSymbol) : slotToString(m_targetArgs[i]);
			columnWidths[m_targetTailSize + i] = std::max(columnWidths[m_targetTailSize + i], slotStr.size() + 1);
		}

		bool const hasExcess = maxStackDepth > m_targetStackSize;

		emitHeader(hasExcess, columnWidths);
		emitSeparatorLine(hasExcess, columnWidths);
		for (auto const& entry: m_entries)
			emitDataRow(entry, hasExcess, columnWidths);
		if (m_truncated)
			m_out << fmt::format("{:>{}}", "...", operationColumnWidth) << "|\n";
		emitSeparatorLine(hasExcess, columnWidths);
		emitTargetRow(hasExcess, columnWidths);
	}

private:
	struct TraceEntry {
		std::string operation;
		TestStack::Data stackAfter;
	};

	std::ostream& m_out;
	std::vector<TraceEntry> m_entries;
	bool m_truncated = false;
	TestStack::Data const& m_targetArgs;
	Liveness const& m_targetTail;
	SpilledVariables const& m_spillSet;
	size_t const m_targetStackSize;
	size_t const m_targetTailSize;
	std::string const m_tailSetStr;
	size_t const m_tailAnnotationWidth;

	void emitTailAnnotationColumn(std::string_view _content, char const _filler, char const _junction) const
	{
		if (m_tailAnnotationWidth == 0)
			return;
		if (_content.empty())
			m_out << std::string(m_tailAnnotationWidth, _filler);
		else
			m_out << fmt::format("{:>{}}", _content, m_tailAnnotationWidth);
		m_out << ' ' << _junction;
	}

	void emitSeparator(size_t const _index, bool const _hasExcess, char const _junction) const
	{
		bool const endOfTargetTail = _index == m_targetTailSize && !m_targetArgs.empty() && m_targetTailSize > 0;
		bool const endOfTargetStackWithExcess = _hasExcess && _index == m_targetTailSize + m_targetArgs.size();
		if (endOfTargetTail || endOfTargetStackWithExcess)
			m_out << ' ' << _junction;
	}

	void emitHeader(bool const _hasExcess, std::vector<std::size_t> const& _columnWidths) const
	{
		m_out << fmt::format("{:>{}}", "", operationColumnWidth) << "|";
		emitTailAnnotationColumn({}, ' ', '|');
		for (std::size_t i = 0; i < _columnWidths.size(); ++i)
		{
			emitSeparator(i, _hasExcess, '|');
			m_out << fmt::format("{:>{}}", i, _columnWidths[i]);
		}
		m_out << "\n";
	}

	void emitSeparatorLine(bool const _hasExcess, std::vector<std::size_t> const& _columnWidths) const
	{
		m_out << fmt::format("{:>{}}", "", operationColumnWidth) << '+';
		emitTailAnnotationColumn({}, '-', '+');
		for (std::size_t i = 0; i < _columnWidths.size(); ++i)
		{
			emitSeparator(i, _hasExcess, '+');
			m_out << std::string(_columnWidths[i], '-');
		}
		m_out << '\n';
	}

	void emitDataRow(TraceEntry const& _entry, bool const _hasExcess, std::vector<std::size_t> const& _columnWidths) const
	{
		m_out << fmt::format("{:>{}}", _entry.operation, operationColumnWidth) << "|";
		emitTailAnnotationColumn({}, ' ', '|');
		for (size_t i = 0; i < _entry.stackAfter.size(); ++i)
		{
			emitSeparator(i, _hasExcess, '|');
			auto const& slot = _entry.stackAfter[i];
			std::string slotStr = slot.isJunk() ? std::string(1, junkSymbol) : slotToString(slot);
			m_out << fmt::format("{:>{}}", slotStr, _columnWidths[i]);
		}
		m_out << '\n';
	}

	void emitTargetRow(bool const _hasExcess, std::vector<size_t> const& _columnWidths) const
	{
		m_out << fmt::format("{:>{}}", "(target)", operationColumnWidth) << "|";

		// Print tail region with set notation
		if (m_targetTailSize > 0 && !(m_targetTail.empty() && m_targetArgs.empty()))
		{
			std::size_t tailWidth = 0;
			for (std::size_t i = 0; i < m_targetTailSize; ++i)
				tailWidth += _columnWidths[i];
			m_out << fmt::format("{:>{}}", m_tailSetStr, tailWidth);

			// Args separator
			if (!m_targetArgs.empty())
				m_out << " |";
		}
		else if (m_targetTailSize == 0)
			emitTailAnnotationColumn(m_tailSetStr, ' ', '|');

		// Print args region
		for (std::size_t i = 0; i < m_targetArgs.size(); ++i)
		{
			auto const& slot = m_targetArgs[i];
			std::string slotStr = slot.isJunk() ? std::string(1, junkSymbol) : slotToString(slot);
			m_out << fmt::format("{:>{}}", slotStr, _columnWidths[m_targetTailSize + i]);
		}

		// Excess separator
		if (_hasExcess)
			m_out << " |";

		m_out << '\n';
	}
};
}

std::unique_ptr<frontend::test::TestCase> ShufflingTest::create(Config const& _config)
{
	return std::make_unique<ShufflingTest>(_config.filename);
}

ShufflingTest::ShufflingTest(std::string const& _filename): TestCase(_filename)
{
	m_source = m_reader.source();
	auto dialectName = m_reader.stringSetting("dialect", "evm");
	soltestAssert(dialectName == "evm");
	m_expectation = m_reader.simpleExpectations();
}

ShufflingTest::TestResult ShufflingTest::run(std::ostream& _stream, std::string const& _linePrefix, bool const _formatted)
{
	auto const testConfig = ShuffleTestInput::parse(m_source);
	if (!testConfig.valid())
	{
		  static constexpr std::string_view formatHelp = R"(initial: [<slot>, ...]
targetStackTop: [<slot>, ...]
targetStackTailSet: {<slot>, ...}
targetStackSize: <non-negative integer>

Where <slot> is one of:
  v<N>    - variable
  phi<N>  - phi node
  lit<N>  - literal
  JUNK    - junk slot

Lines starting with // are comments. Comments at the end of lines are supported, too.
The targetStackTailSet defaults to empty.
The targetStackSize defaults to the size of targetStackTop if targetStackTailSet is empty, otherwise it must be
explicitly provided.)";
		util::AnsiColorized out(_stream, _formatted, {util::formatting::BOLD, util::formatting::RED});
		out	<< _linePrefix << fmt::format("Error parsing source. Expected format:") << '\n';

		for (auto const line: ranges::views::split(formatHelp, '\n'))
		{
			auto const lineSVBegin = ranges::begin(line);
			auto const lineSVEnd = ranges::end(line);
			std::string_view lineSV;
			if (lineSVBegin != lineSVEnd)
				lineSV = {&*lineSVBegin, static_cast<std::size_t>(ranges::distance(lineSVBegin, lineSVEnd))};
			out << _linePrefix << "  " << lineSV << '\n';
		}
		return TestResult::FatalError;
	}

	auto stackData = *testConfig.initial;
	std::ostringstream oss;
	StackShufflerResult shuffleResult;
	SpilledVariables spillSet = testConfig.initialSpilledSet;

	// First, when spilling is allowed, run the shuffler repeatedly without recording to determine
	// the final spill set. Each iteration starts from the initial stack and adds the culprit of a
	// recoverable StackTooDeep to the spill set.
	if (testConfig.allowSpilling)
		while (true)
		{
			auto scratch = *testConfig.initial;
			TestStack stack(scratch, {});
			auto const result = StackShuffler<StackManipulationCallbacks>::shuffle(
				stack,
				*testConfig.targetStackTop,
				testConfig.targetStackTailSet,
				*testConfig.targetStackSize,
				&spillSet
			);
			if (
				result.status != StackShufflerResult::Status::StackTooDeep
			)
				break;
			spillSet.spill(result.culprit.valueID());
		}

	// Final shuffle with the (possibly pre-populated) spill set, recording the trace.
	{
		TraceRecorder trace(oss, *testConfig.targetStackTop, testConfig.targetStackTailSet, *testConfig.targetStackSize, spillSet);
		trace.record("(initial)", *testConfig.initial);
		TestStack stack(stackData, {.hook = [&](std::string const& op)
		{
			trace.record(op, stackData);
		}});
		shuffleResult = StackShuffler<StackManipulationCallbacks>::shuffle(
			stack,
			*testConfig.targetStackTop,
			testConfig.targetStackTailSet,
			*testConfig.targetStackSize,
			&spillSet
		);
		if (shuffleResult.status == StackShufflerResult::Status::MaxIterationsReached)
			trace.truncate(30);
	}

	switch (shuffleResult.status)
	{
	case StackShufflerResult::Status::Admissible:
		oss << "Status: Admissible\n";
		break;
	case StackShufflerResult::Status::StackTooDeep:
		oss << fmt::format("Status: StackTooDeep (culprit: {})\n", slotToString(shuffleResult.culprit));
		break;
	case StackShufflerResult::Status::MaxIterationsReached:
		oss << "Status: MaxIterationsReached\n";
		break;
	case StackShufflerResult::Status::Continue:
		yulAssert(false, "Unexpected Continue status from shuffle()");
	}
	if (testConfig.allowSpilling)
		oss << fmt::format(
			"Spilled: {{{}}}\n",
			fmt::join(
				spillSet.spilledValues() | ranges::views::transform(
					[](auto const& id) { return slotToString(StackSlot::makeValueID(id)); }
				),
				", "
			)
		);
	// check stack data
	if (shuffleResult.status == StackShufflerResult::Status::Admissible)
	{
		yulAssert(*testConfig.targetStackSize >= testConfig.targetStackTop->size());
		auto const tailSize = *testConfig.targetStackSize - testConfig.targetStackTop->size();
		yulAssert(stackData.size() == *testConfig.targetStackSize);
		for (const auto& valueID: testConfig.targetStackTailSet | ranges::views::keys)
		{
			if (spillSet.isSpilled(valueID))
				continue;
			auto const findIt = ranges::find(
				stackData.begin(),
				stackData.begin() + static_cast<std::ptrdiff_t>(tailSize),
				StackSlot::makeValueID(valueID)
			);
			yulAssert(findIt != ranges::end(stackData));
		}
		for (std::size_t offset = tailSize; offset < *testConfig.targetStackSize; ++offset)
		{
			auto const& targetSlot = testConfig.targetStackTop->at(offset - tailSize);
			yulAssert(targetSlot.isJunk() || stackData[offset] == targetSlot);
		}
	}
	m_obtainedResult = oss.str();


	return checkResult(_stream, _linePrefix, _formatted);
}
