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

#include <test/libyul/ssa/SpillCodegenTest.h>

#include <test/libyul/Common.h>
#include <test/Common.h>

#include <libyul/backends/evm/ssa/CodeTransform.h>
#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>
#include <libyul/backends/evm/ssa/SSACFGBuilder.h>
#include <libyul/backends/evm/ssa/transform/OptimizationPipeline.h>

#include <libyul/backends/evm/EthAssemblyAdapter.h>
#include <libyul/backends/evm/EVMBuiltins.h>
#include <libyul/backends/evm/EVMDialect.h>

#include <libyul/Object.h>
#include <libyul/YulStack.h>

#include <libevmasm/Assembly.h>

#include <fmt/format.h>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::test::ssa;

namespace
{

std::string describeCFG(SSACFG const& _cfg, SpillReport::PerCFG const& _entry)
{
	std::string label = _entry.isMainGraph ? "<main>" : _entry.functionName;
	std::string out = fmt::format("// CFG[{}] {}\n", _entry.graphID, label);

	if (_entry.spillSet.numSpilled() == 0)
	{
		out += "//   spilled: none\n";
		return out;
	}

	out += "//   spilled:\n";
	for (auto const& [value, address]: _entry.addresses)
		out += fmt::format(
			"//     {} ({}) -> mem {}\n",
			value,
			_cfg.isPhi(value) ? "phi" : "value",
			toCompactHexWithPrefix(address)
		);

	out += "//   mstore schedule (owner written at definition of emitAt):\n";
	for (InstId const value: _entry.spillSet.spilledValues())
	{
		SSACFG::BlockId const block = _cfg.inst(value).block;
		out += fmt::format(
			"//     mstore addr({}) <- {} @ emitAt={} (B#{})\n",
			value,
			value,
			value,
			block.value
		);
	}
	return out;
}

}

std::unique_ptr<frontend::test::TestCase> SpillCodegenTest::create(Config const& _config)
{
	return std::make_unique<SpillCodegenTest>(_config.filename);
}

SpillCodegenTest::SpillCodegenTest(std::string const& _filename): TestCase(_filename)
{
	m_source = m_reader.source();
	auto const dialectName = m_reader.stringSetting("dialect", "evm");
	soltestAssert(dialectName == "evm");
	m_expectation = m_reader.simpleExpectations();
}

frontend::test::TestCase::TestResult SpillCodegenTest::run(std::ostream& _stream, std::string const& _linePrefix, bool const _formatted)
{
	static std::string_view constexpr OBJECT_SEPARATOR = "\n>>>>> OBJECT SEPARATOR\n";

	YulStack const yulStack = yul::test::parseYul(m_source);
	if (yulStack.hasErrors())
	{
		yul::test::printYulErrors(yulStack, _stream, _linePrefix, _formatted);
		return TestResult::FatalError;
	}

	std::set<Object const*> visited;
	visited.insert(yulStack.parserResult().get());

	std::vector<Object const*> toVisit{yulStack.parserResult().get()};
	while (!toVisit.empty())
	{
		auto const& object = *toVisit.back();
		toVisit.pop_back();

		auto const* evmDialect = dynamic_cast<EVMDialect const*>(object.dialect());
		yulAssert(evmDialect);

		std::unique_ptr<ControlFlowGraphs> controlFlowGraphs = SSACFGBuilder::build(
			*object.analysisInfo,
			*evmDialect,
			object.code()->root(),
			false
		);
		yul::ssa::transform::optimize(*controlFlowGraphs);
		ControlFlowGraphsLiveness const liveness(*controlFlowGraphs);

		evmasm::Assembly assembly(
			solidity::test::CommonOptions::get().evmVersion(),
			true,
			solidity::test::CommonOptions::get().eofVersion(),
			object.name
		);
		EthAssemblyAdapter adapter(assembly);
		BuiltinContext context;
		context.currentObject = &object;

		SpillReport report;
		CodeTransform::run(adapter, *controlFlowGraphs, liveness, context, &report);

		if (!m_obtainedResult.empty())
			m_obtainedResult += OBJECT_SEPARATOR;
		m_obtainedResult += fmt::format("// object \"{}\"\n", object.name);
		m_obtainedResult += "// ===== SSA CFG =====\n";
		m_obtainedResult += controlFlowGraphs->print();
		m_obtainedResult += "// ===== spill info =====\n";
		for (SpillReport::PerCFG const& entry: report.perCFG)
			m_obtainedResult += describeCFG(*controlFlowGraphs->functionGraphs[entry.graphID], entry);
		m_obtainedResult += "// ===== assembly =====\n";
		m_obtainedResult += assembly.assemblyString();

		for (auto const& subNode: object.subObjects)
			if (auto subObject = std::dynamic_pointer_cast<Object>(subNode))
				if (!visited.contains(subObject.get()))
				{
					visited.insert(subObject.get());
					toVisit.push_back(subObject.get());
				}
	}

	return checkResult(_stream, _linePrefix, _formatted);
}
