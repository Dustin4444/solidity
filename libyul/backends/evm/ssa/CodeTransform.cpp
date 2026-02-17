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

#include <libyul/backends/evm/ssa/CodeTransform.h>

using namespace solidity::yul::ssa;


std::vector<solidity::yul::StackTooDeepError> CodeTransform::run
(
	AbstractAssembly& _assembly,
	ControlFlowLiveness const& _controlFlowLiveness,
	BuiltinContext& _builtinContext,
	UseNamedLabels _useNamedLabelsForFunctions
)
{
	yulAssert(!_controlFlowLiveness.cfgLiveness.empty());
	ControlFlow const& controlFlow = _controlFlowLiveness.controlFlow.get();
	yulAssert(controlFlow.functionGraphs.size() == _controlFlowLiveness.cfgLiveness.size());
	FunctionLabels const functionLabels = registerFunctionLabels(_assembly, controlFlow, _useNamedLabelsForFunctions);
	CodeTransform mainCodeTransform(
		_assembly,
		_builtinContext, functionLabels, *controlFlow.mainGraph(), *_controlFlowLiveness.cfgLiveness.front()
	);
	return {};
}
CodeTransform::FunctionLabels CodeTransform::registerFunctionLabels(
	AbstractAssembly& _assembly, ControlFlow const& _controlFlow, UseNamedLabels _useNamedLabelsForFunctions)
{
	FunctionLabels functionLabels;

	for (auto const& [_function, _functionGraph]: _controlFlow.functionGraphMapping)
	{
		if (!_function)
			continue;
		std::set<YulString> assignedFunctionNames;
		bool nameAlreadySeen = !assignedFunctionNames.insert(_function->name).second;
		if (_useNamedLabelsForFunctions == UseNamedLabels::YesAndForceUnique)
			yulAssert(!nameAlreadySeen);
		bool useNamedLabel = _useNamedLabelsForFunctions != UseNamedLabels::Never && !nameAlreadySeen;
		functionLabels[_function] = useNamedLabel ?
			_assembly.namedLabel(
				_function->name.str(),
				_functionGraph->arguments.size(),
				_functionGraph->returns.size(),
				_functionGraph->debugData ? _functionGraph->debugData->astID : std::nullopt
			) :
			_assembly.newLabelId();
	}
	return functionLabels;
}
