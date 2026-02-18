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

#include <libyul/backends/evm/ssa/ExactShuffler.h>

#include <libsolutil/CommonData.h>



using namespace solidity::yul;
using namespace solidity::yul::ssa;

ReversePhiFunctionTransform::ReversePhiFunctionTransform(
	SSACFG const& _cfg,
	SSACFG::BlockId const _from,
	SSACFG::BlockId const _to
)
{
	for (auto const& upsilon: _cfg.block(_from).upsilons)
		if (_cfg.phiInfo(upsilon.phi).block == _to)
			m_reversePhiMap[upsilon.phi] = upsilon.value;
}

bool ReversePhiFunctionTransform::noOp() const
{
	return m_reversePhiMap.empty();
}

SSACFG::ValueId ReversePhiFunctionTransform::operator()(SSACFG::ValueId _valueId) const
{
	return util::valueOrDefault(m_reversePhiMap, _valueId, _valueId);
}
