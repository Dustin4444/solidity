#include <libsolutil/logging.h>

#include <map>
#include <optional>
#include <vector>

using namespace solidity;

struct Registry::Impl {
	std::map<std::string, std::unique_ptr<Logger>, std::less<>> loggers;
	// Preset levels: applied to new loggers on creation (most specific match wins)
	std::vector<std::pair<std::string, LogLevel>> presets;
	std::FILE* output = stderr;

	LogLevel resolve_level(std::string const& name) const {
		LogLevel result = LogLevel::info;
		std::size_t best_match = 0;
		bool matched = false;
		for (auto const& [prefix, level] : presets) {
			if (prefix.empty() || name == prefix || name.starts_with(prefix + ".")) {
				if (!matched || prefix.size() >= best_match) {
					best_match = prefix.size();
					result = level;
					matched = true;
				}
			}
		}
		return result;
	}
};

Registry::Registry() : m_impl(std::make_unique<Impl>()) {}
Registry::~Registry() = default;

Registry& Registry::instance() {
	static Registry reg;
	return reg;
}

Logger& Registry::get(std::string const& name) {
	auto it = m_impl->loggers.find(name);
	if (it != m_impl->loggers.end())
		return *it->second;

	auto level = m_impl->resolve_level(name);
	auto logger = std::make_unique<Logger>(name, level, m_impl->output);
	auto& ref = *logger;
	m_impl->loggers.emplace(name, std::move(logger));
	return ref;
}

void Registry::set_level(std::string_view prefix, LogLevel level) {
	// Store as preset for future loggers
	std::string key(prefix);
	bool found = false;
	for (auto& [p, l] : m_impl->presets) {
		if (p == key) { l = level; found = true; break; }
	}
	if (!found)
		m_impl->presets.emplace_back(key, level);

	// Apply to existing loggers
	for (auto& [name, logger] : m_impl->loggers) {
		if (key.empty() || name == prefix || name.starts_with(key + "."))
			logger->set_level(level);
	}
}

void Registry::set_output(std::FILE* output) {
	m_impl->output = output;
}

void Registry::clear() {
	m_impl->loggers.clear();
}

namespace solidity {

std::optional<LogLevel> parseLogLevel(std::string_view s) {
	if (s == "trace") return LogLevel::trace;
	if (s == "debug") return LogLevel::debug;
	if (s == "info")  return LogLevel::info;
	if (s == "warn")  return LogLevel::warn;
	if (s == "error") return LogLevel::error;
	if (s == "off")   return LogLevel::off;
	return std::nullopt;
}

}
