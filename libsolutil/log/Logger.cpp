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

#include <libsolutil/log/Logger.h>

#include <fmt/ostream.h>

#include <ostream>
#include <utility>

using namespace solidity::log;

Logger::Logger(std::string _name, Level const _level, std::ostream& _output):
	m_name(std::move(_name)),
	m_level(_level),
	m_output(&_output)
{
}

void Logger::emit(fmt::string_view const _fmt, fmt::format_args const _args, bool const _newline) const
{
	fmt::vprint(*m_output, _fmt, _args);
	if (_newline)
		m_output->put('\n');
}
