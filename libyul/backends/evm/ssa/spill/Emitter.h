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

#include <libyul/backends/evm/AbstractAssembly.h>
#include <libyul/backends/evm/ssa/SSACFG.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>
#include <libyul/backends/evm/ssa/Stack.h>
#include <libyul/backends/evm/ssa/StackShuffler.h>
#include <libyul/backends/evm/ssa/spill/MemoryAddressing.h>
#include <libyul/backends/evm/ssa/spill/SpillSet.h>

#include <libevmasm/Instruction.h>

#include <libyul/Exceptions.h>

#include <fmt/format.h>

namespace solidity::yul::ssa::spill
{

class Emitter
{
public:
	Emitter(
		MemoryAddressing const& _addressing,
		FunctionGraphID const _cfgIdx,
		SpillSet const& _spillSet,
		SSACFG const& _cfg,
		AbstractAssembly& _assembly
	):
		m_addressing(&_addressing),
		m_cfgIdx(_cfgIdx),
		m_spillSet(&_spillSet),
		m_cfg(&_cfg),
		m_assembly(&_assembly)
	{
	}

	void emitLoad(InstId const _value) const
	{
		// god-mode execute mload
		u256 const addr = m_addressing->addressOf(m_cfgIdx, _value);
		m_assembly->appendConstant(addr);
		m_assembly->appendInstruction(evmasm::Instruction::MLOAD);
	}

	template<StackManipulationCallbackConcept Callback>
	void emitStoresAt(InstId _at, Stack<Callback>& _stack) const
	{
		if (!m_spillSet->isSpilled(_at))
			return;

		StackData const restoreTarget = _stack.data();
		StackSlot const sourceSlot = StackSlot::makeValue(*m_cfg, _at);
		u256 const addr = m_addressing->addressOf(m_cfgIdx, _at);

		// addr(_at) is the slot THIS `mstore` populates so we have to exclude it from the spill set to hinder the
		// shuffler from just `mload`ing it
		SpillSet const emitLeafSpills = m_spillSet->without(_at);

		// Positional target = restoreTarget with `sourceSlot` pushed on top
		StackData target = restoreTarget;
		target.push_back(sourceSlot);

		auto const shuffleResult = StackShuffler<Callback>::shuffle(_stack, target, &emitLeafSpills);
		yulAssert(
			shuffleResult.status == StackShufflerResult::Status::Admissible,
			fmt::format(
				"shuffler failed to bring spilled value {} to top (status={})",
				_at, static_cast<int>(shuffleResult.status)
			)
		);

		{
			// god-mode push address
			m_assembly->appendConstant(addr);
			_stack.template push<false>(StackSlot::makeJunk());
		}
		{
			// god-mode execute mstore
			m_assembly->appendInstruction(evmasm::Instruction::MSTORE);
			_stack.template pop<false>();
			_stack.template pop<false>();
		}
	}

private:
	MemoryAddressing const* m_addressing;
	FunctionGraphID m_cfgIdx;
	SpillSet const* m_spillSet;
	SSACFG const* m_cfg;
	AbstractAssembly* m_assembly;
};

}

