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

#include <libsolutil/Noinline.h>

#include <fmt/base.h>

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace solidity::log
{

enum class Level: std::uint8_t
{
	trace = 0,
	debug = 1,
	warn = 2,
	off = 3
};

constexpr bool operator>=(Level _a, Level _b) noexcept
{
	return static_cast<std::uint8_t>(_a) >= static_cast<std::uint8_t>(_b);
}

class Logger
{
public:
	Logger(std::string _name, Level _level, std::ostream& _output);

	bool shouldLog(Level _level) const noexcept { return _level >= m_level; }

	void setLevel(Level _level) noexcept { m_level = _level; }
	void setOutput(std::ostream& _output) noexcept { m_output = &_output; }

	Level level() const noexcept { return m_level; }
	std::string_view name() const noexcept { return m_name; }

	template<typename... Args>
	SOL_NOINLINE void trace(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), true);
	}

	template<typename... Args>
	SOL_NOINLINE void debug(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), true);
	}

	template<typename... Args>
	SOL_NOINLINE void warn(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), true);
	}

	/// Writes a log fragment without appending a newline. Intended for code
	/// that assembles a single log line from several pieces.
	template<typename... Args>
	SOL_NOINLINE void traceRaw(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), false);
	}

	template<typename... Args>
	SOL_NOINLINE void debugRaw(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), false);
	}

	template<typename... Args>
	SOL_NOINLINE void warnRaw(fmt::format_string<Args...> _fmt, Args&&... _args) const
	{
		emit(_fmt, fmt::make_format_args(_args...), false);
	}

private:
	void emit(fmt::string_view _fmt, fmt::format_args _args, bool _newline) const;

	std::string m_name;
	Level m_level;
	std::ostream* m_output;
};

}

/// Guarded log macro: does not evaluate @a ... when the logger is below @a lvl.
/// @a lvl must be an unqualified enumerator of ::solidity::log::Level
/// (`trace`, `debug`, or `warn`). Example:
///   solLog(logger, debug, "x={}", expensive());
#define solLog(logger, lvl, ...) \
	do \
	{ \
		if ((logger).shouldLog(::solidity::log::Level::lvl)) [[unlikely]] \
			(logger).lvl(__VA_ARGS__); \
	} \
	while (false)

#define solTrace(logger, ...) solLog(logger, trace, __VA_ARGS__)
#define solDebug(logger, ...) solLog(logger, debug, __VA_ARGS__)
#define solWarn(logger, ...) solLog(logger, warn, __VA_ARGS__)

/// Same as solLog but routes to the *Raw helpers, which do not append a
/// newline. Intended for assembling a single log line from several fragments.
#define solLogRaw(logger, lvl, ...) \
	do \
	{ \
		if ((logger).shouldLog(::solidity::log::Level::lvl)) [[unlikely]] \
			(logger).lvl##Raw(__VA_ARGS__); \
	} \
	while (false)

#define solTraceRaw(logger, ...) solLogRaw(logger, trace, __VA_ARGS__)
#define solDebugRaw(logger, ...) solLogRaw(logger, debug, __VA_ARGS__)
#define solWarnRaw(logger, ...) solLogRaw(logger, warn, __VA_ARGS__)
