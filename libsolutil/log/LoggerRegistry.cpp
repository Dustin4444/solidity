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

#include <libsolutil/log/LoggerRegistry.h>

#include <range/v3/view/map.hpp>

#include <iostream>
#include <map>
#include <utility>

using namespace solidity::log;

namespace
{

bool prefixMatches(std::string_view const _prefix, std::string_view const _name)
{
	if (_prefix.empty())
		return true;
	if (_name == _prefix)
		return true;
	return
		_name.size() > _prefix.size() &&
		_name.starts_with(_prefix) &&
		_name[_prefix.size()] == '.';
}

}

Level solidity::log::resolveLevel(
	std::string_view _name,
	std::vector<std::pair<std::string, Level>> const& _presets
)
{
	Level result = Level::off;
	std::size_t bestMatch = 0;
	for (auto const& [prefix, level]: _presets)
		if (prefixMatches(prefix, _name) && prefix.size() >= bestMatch)
		{
			bestMatch = prefix.size();
			result = level;
		}
	return result;
}

struct LoggerRegistry::Impl
{
	std::map<std::string, std::unique_ptr<Logger>, std::less<>> loggers;
	std::vector<std::pair<std::string, Level>> presets;
	std::ostream* output = &std::cerr;
};

LoggerRegistry::LoggerRegistry(): m_impl(std::make_unique<Impl>()) {}
LoggerRegistry::~LoggerRegistry() = default;

LoggerRegistry& LoggerRegistry::instance()
{
	static LoggerRegistry registry;
	return registry;
}

Logger const& LoggerRegistry::get(std::string_view const _name)
{
	if (
		auto const it = m_impl->loggers.find(_name);
		it != m_impl->loggers.end()
	)
		return *it->second;

	Level const level = resolveLevel(_name, m_impl->presets);
	auto logger = std::make_unique<Logger>(std::string(_name), level, *m_impl->output);
	Logger const& ref = *logger;
	m_impl->loggers.emplace(_name, std::move(logger));
	return ref;
}

void LoggerRegistry::setLevel(std::string_view _prefix, Level _level)
{
	bool updated = false;
	for (auto& [prefix, level]: m_impl->presets)
		if (prefix == _prefix)
		{
			level = _level;
			updated = true;
			break;
		}
	if (!updated)
		m_impl->presets.emplace_back(std::string(_prefix), _level);

	for (auto const& [name, logger]: m_impl->loggers)
		logger->setLevel(resolveLevel(name, m_impl->presets));
}

void LoggerRegistry::setOutput(StandardStream _stream)
{
	std::ostream& output = _stream == StandardStream::Stderr ? std::cerr : std::cout;
	m_impl->output = &output;
	for (auto const& logger: m_impl->loggers | ranges::views::values)
		logger->setOutput(output);
}

std::optional<Level> solidity::log::parseLevel(std::string_view _text)
{
	if (_text == "trace") return Level::trace;
	if (_text == "debug") return Level::debug;
	if (_text == "warn") return Level::warn;
	if (_text == "off") return Level::off;
	return std::nullopt;
}
