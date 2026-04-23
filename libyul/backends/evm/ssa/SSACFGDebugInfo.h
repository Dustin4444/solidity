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

#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <liblangutil/DebugData.h>
#include <libyul/Exceptions.h>

#include <map>
#include <vector>

namespace solidity::yul::ssa
{

/// Stores debug data for all SSACFG entities in parallel arrays indexed by the entities' stable IDs.
struct SSACFGDebugInfo
{
	void setBlockDebugData(BlockId _id, langutil::DebugData::ConstPtr _data)
	{
		yulAssert(_id.hasValue());
		ensureSize(m_blockDebugData, _id.value);
		m_blockDebugData[_id.value] = std::move(_data);
	}

	langutil::DebugData::ConstPtr const& blockDebugData(BlockId _id) const
	{
		return getOrEmpty(m_blockDebugData, _id.value);
	}

	void setExitDebugData(BlockId _id, langutil::DebugData::ConstPtr _data)
	{
		yulAssert(_id.hasValue());
		ensureSize(m_exitDebugData, _id.value);
		m_exitDebugData[_id.value] = std::move(_data);
	}

	langutil::DebugData::ConstPtr const& exitDebugData(BlockId _id) const
	{
		return getOrEmpty(m_exitDebugData, _id.value);
	}

	void setInstDebugData(InstId _id, langutil::DebugData::ConstPtr _data)
	{
		yulAssert(_id.hasValue());
		ensureSize(m_instDebugData, _id.value);
		m_instDebugData[_id.value] = std::move(_data);
	}

	langutil::DebugData::ConstPtr const& instDebugData(InstId _id) const
	{
		return getOrEmpty(m_instDebugData, _id.value);
	}

	void setValueDebugData(ValueId _id, langutil::DebugData::ConstPtr _data)
	{
		yulAssert(_id.hasValue());
		m_valueDebugData[_id] = std::move(_data);
	}

	langutil::DebugData::ConstPtr const& valueDebugData(ValueId _id) const
	{
		static langutil::DebugData::ConstPtr const empty{nullptr};
		auto const it = m_valueDebugData.find(_id);
		if (it == m_valueDebugData.end())
			return empty;
		return it->second;
	}

	/// Top-level debug data for the SSACFG (e.g. the function definition's debug data).
	langutil::DebugData::ConstPtr graphDebugData;

private:
	static void ensureSize(std::vector<langutil::DebugData::ConstPtr>& _vec, std::size_t const _index)
	{
		if (_vec.size() <= _index)
			_vec.resize(_index + 1);
	}

	static langutil::DebugData::ConstPtr const& getOrEmpty(
		std::vector<langutil::DebugData::ConstPtr> const& _debugData,
		std::size_t const _index
	)
	{
		static langutil::DebugData::ConstPtr const empty{nullptr};
		if (_index >= _debugData.size())
			return empty;
		return _debugData[_index];
	}

	std::vector<langutil::DebugData::ConstPtr> m_blockDebugData;
	std::vector<langutil::DebugData::ConstPtr> m_exitDebugData;
	std::vector<langutil::DebugData::ConstPtr> m_instDebugData;
	std::map<ValueId, langutil::DebugData::ConstPtr> m_valueDebugData;
};

}
