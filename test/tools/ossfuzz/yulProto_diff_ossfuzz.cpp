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

#include <fstream>

#include <test/tools/ossfuzz/yulProto.pb.h>
#include <test/tools/fuzzer_common.h>
#include <test/tools/ossfuzz/protoToYul.h>

#include <test/libyul/YulOptimizerTestCommon.h>

#include <src/libfuzzer/libfuzzer_macro.h>

#include <libyul/AST.h>
#include <libyul/YulStack.h>
#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/backends/evm/ConstantOptimiser.h>
#include <libyul/backends/evm/EVMMetrics.h>
#include <libyul/Exceptions.h>
#include <libyul/optimiser/Suite.h>

#include <libsolidity/interface/OptimiserSettings.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/EVMVersion.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <test/tools/ossfuzz/yulFuzzerCommon.h>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::langutil;
using namespace solidity::yul;
using namespace solidity::yul::test;
using namespace solidity::yul::test::yul_fuzzer;
using namespace solidity::frontend;

namespace
{
/// Maps a sequence of uint32 values to a Yul optimizer step abbreviation string.
/// Each value is taken modulo the number of valid step characters.
/// If the sequence has >= 4 elements, the middle half is wrapped in [...] to
/// test the "repeat until stable" behaviour.
std::string buildOptimizerSequence(google::protobuf::RepeatedField<google::protobuf::uint32> const& _steps)
{
	// All 32 valid step abbreviations (matches Suite.cpp stepNameToAbbreviationMap)
	static std::string const validChars = "flcCUnDEvejsxIOoighFTLMrSmVatpud";

	if (_steps.empty())
		return OptimiserSettings::DefaultYulOptimiserSteps;

	std::string seq;
	seq.reserve(static_cast<size_t>(_steps.size()) + 2);
	for (auto const s: _steps)
		seq += validChars[s % validChars.size()];

	// Wrap middle portion in brackets to also fuzz iterative stabilization.
	if (_steps.size() >= 4)
	{
		size_t const n = seq.size();
		size_t const lo = n / 4;
		size_t const hi = (3 * n) / 4;
		seq = seq.substr(0, lo) + "[" + seq.substr(lo, hi - lo) + "]" + seq.substr(hi);
	}

	return seq;
}
} // anonymous namespace

DEFINE_PROTO_FUZZER(Program const& _input)
{
	ProtoConverter converter(false, true);
	std::string yul_source = converter.programToString(_input);
	EVMVersion version = converter.version();
	auto calldata = converter.calldata();

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate a YUL source file x.yul:
		// PROTO_FUZZER_DUMP_PATH=x.yul ./a.out proto-input
		std::ofstream of(dump_path);
		of.write(yul_source.data(), static_cast<std::streamsize>(yul_source.size()));
	}

	YulStringRepository::reset();

	// YulStack entry point
	YulStack stack(
		version,
		std::nullopt,
		solidity::frontend::OptimiserSettings::full(),
		DebugInfoSelection::All()
	);

	// Parse protobuf mutated YUL code
	if (
		!stack.parseAndAnalyze("source", yul_source) ||
		!stack.parserResult()->code() ||
		!stack.parserResult()->analysisInfo ||
		Error::containsErrors(stack.errors())
	)
	{
		SourceReferenceFormatter{std::cout, stack, false, false}.printErrorInformation(stack.errors());
		yulAssert(false, "Proto fuzzer generated malformed program");
	}

	// Generate pseudo-random calldata using libfuzzer input
	std::ostringstream os1;
	std::ostringstream os2;
	// Disable memory tracing to avoid false positive reports
	// such as unused write to memory e.g.,
	// { mstore(0, 1) }
	// that would be removed by the redundant store eliminator.
	yulFuzzerUtil::TerminationReason termReason = yulFuzzerUtil::interpret(
		os1,
		calldata,
		*stack.parserResult()->code(),
		/*disableMemoryTracing=*/true,
		/*outputStorageOnly=*/false,
		yulFuzzerUtil::maxSteps,
		yulFuzzerUtil::maxTraceSize,
		yulFuzzerUtil::maxExprNesting,
		yulFuzzerUtil::maxCost
	);

	if (yulFuzzerUtil::resourceLimitsExceeded(termReason))
		return;

	try {
		if (_input.optimiser_seq_size() > 0)
		{
			// Derive optimizer configuration from the protobuf input.
			std::string const optimizerSeq = buildOptimizerSequence(_input.optimiser_seq());
			std::string const cleanupSeq = _input.optimiser_cleanup_seq_size() > 0
				? buildOptimizerSequence(_input.optimiser_cleanup_seq())
				: std::string(OptimiserSettings::DefaultYulOptimiserCleanupSteps);

			bool const optimizeStackAllocation = _input.has_optimize_stack_allocation()
				? _input.optimize_stack_allocation()
				: true;

			// expected_executions == 0 means creation code (nullopt); otherwise use the value.
			std::optional<size_t> const expectedExecutions = _input.has_expected_executions()
				? (_input.expected_executions() == 0
					? std::nullopt
					: std::optional<size_t>(_input.expected_executions()))
				: std::optional<size_t>(200);

			bool const isCreation = _input.has_is_creation() ? _input.is_creation() : false;
			size_t const meterRuns = (expectedExecutions.has_value() && !isCreation)
				? *expectedExecutions
				: 200;

			// Copy the parsed object so we can optimize it independently.
			auto optimizedObject = std::make_shared<yul::Object>(*stack.parserResult());
			EVMDialect const& dialect = dynamic_cast<EVMDialect const&>(*optimizedObject->dialect());
			GasMeter meter(dialect, isCreation, meterRuns);
			OptimiserSuite::run(
				&meter,
				*optimizedObject,
				optimizeStackAllocation,
				optimizerSeq,
				cleanupSeq,
				expectedExecutions
			);
			termReason = yulFuzzerUtil::interpret(
				os2,
				calldata,
				*optimizedObject->code(),
				true
			);
		}
		else
		{
			// Fallback: single named step (original behaviour, keeps old corpus working).
			YulOptimizerTestCommon optimizerTest(stack.parserResult());
			optimizerTest.setStep(optimizerTest.randomOptimiserStep(_input.step()));
			auto const* astRoot = optimizerTest.run();
			yulAssert(astRoot != nullptr, "Optimiser error.");
			termReason = yulFuzzerUtil::interpret(
				os2,
				calldata,
				*optimizerTest.optimizedObject()->code(),
				true
			);
		}

		if (yulFuzzerUtil::resourceLimitsExceeded(termReason))
			return;

		bool isTraceEq = (os1.str() == os2.str());
		if (!isTraceEq)
		{
			std::cout << os1.str() << std::endl;
			std::cout << os2.str() << std::endl;
			yulAssert(false, "Interpreted traces for optimized and unoptimized code differ.");
		}
		return;

	} catch (langutil::UnimplementedFeatureError const&) {
	    // Unimplemented feature, skip this input
	    return;
	} catch (yul::OptimizerException const&) {
	    // Generated sequence was invalid (e.g. step prerequisites not met), skip.
	    return;
	}
}
