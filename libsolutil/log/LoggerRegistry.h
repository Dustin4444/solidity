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

#include <libsolutil/log/Logger.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace solidity::log
{

enum class StandardStream { Stderr, Stdout };

/// Resolves the level for category @a _name against a list of @a _presets
Level resolveLevel(
	std::string_view _name,
	std::vector<std::pair<std::string, Level>> const& _presets
);

class LoggerRegistry
{
public:
	static LoggerRegistry& instance();

	LoggerRegistry(LoggerRegistry const&) = delete;
	LoggerRegistry& operator=(LoggerRegistry const&) = delete;
	LoggerRegistry(LoggerRegistry&&) = delete;
	LoggerRegistry& operator=(LoggerRegistry&&) = delete;

	/// Returns the logger for @a _name, creating it lazily on first use.
	/// The returned reference is stable for the lifetime of the registry.
	Logger const& get(std::string_view _name);

	/// Applies @a _level to all loggers whose name equals or descends from @a _prefix.
	/// The empty prefix is the global default. The most specific preset wins.
	void setLevel(std::string_view _prefix, Level _level);

	/// Redirects every existing and future logger to the selected standard stream.
	void setOutput(StandardStream _stream);

private:
	LoggerRegistry();
	~LoggerRegistry();

	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

/// Parses the log level. Returns nullopt on mismatch.
std::optional<Level> parseLevel(std::string_view _text);

}

/// Declares a file-local logger reference bound to a registry entry. Should be
/// placed, e.g., at namespace scope.
#define DEFINE_LOGGER(varName, categoryName) \
	static ::solidity::log::Logger const& varName = ::solidity::log::LoggerRegistry::instance().get(categoryName)
