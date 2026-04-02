#pragma once

#include <libsolutil/Exceptions.h>

#include <fmt/format.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace solidity
{

enum class LogLevel : std::uint8_t { trace = 0, debug = 1, info = 2, warn = 3, error = 4, off = 5 };

/// Guard macro: skips argument evaluation when the logger's level is too low.
///   SOL_LOG(logger, debug, "x={} y={}", expensive1(), expensive2());
#define SOL_LOG(logger, lvl, ...) \
    do { if ((logger).should_log(::solidity::LogLevel::lvl)) [[unlikely]] (logger).lvl(__VA_ARGS__); } while (0)

class Logger {
public:
    explicit Logger(std::string name, LogLevel level = LogLevel::info, std::FILE* output = stdout)
        : m_name(std::move(name)),
          m_level(static_cast<std::uint8_t>(level)),
          m_output(output) {}

    [[nodiscard]] bool should_log(LogLevel level) const noexcept {
        return static_cast<std::uint8_t>(level) >= m_level;
    }

    void set_level(LogLevel level) noexcept {
        m_level = static_cast<std::uint8_t>(level);
    }

    [[nodiscard]] LogLevel level() const noexcept {
        return static_cast<LogLevel>(m_level);
    }

    [[nodiscard]] std::string const& name() const noexcept { return m_name; }

    void set_output(std::FILE* output) noexcept { m_output = output; }

    template <typename... Args>
    void log(LogLevel const level, fmt::format_string<Args...> fmt, Args&&... args) const {
        if (!should_log(level))
            return;
        fmt::print(m_output, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    SOL_NOINLINE void trace(fmt::format_string<Args...> fmt, Args&&... args) const {
        log(LogLevel::trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    SOL_NOINLINE void debug(fmt::format_string<Args...> fmt, Args&&... args) const {
        log(LogLevel::debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    SOL_NOINLINE void info(fmt::format_string<Args...> fmt, Args&&... args) const {
        log(LogLevel::info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    SOL_NOINLINE void warn(fmt::format_string<Args...> fmt, Args&&... args) const {
        log(LogLevel::warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    SOL_NOINLINE void error(fmt::format_string<Args...> fmt, Args&&... args) const {
        log(LogLevel::error, fmt, std::forward<Args>(args)...);
    }

private:
    std::string m_name;
    std::uint8_t m_level;
    std::FILE* m_output;
};

class Registry {
public:
    static Registry& instance();

    Logger& get(std::string const& name);
    void set_level(std::string_view prefix, LogLevel level);
    void set_output(std::FILE* output);
    void clear();

private:
    Registry();
    ~Registry();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::optional<LogLevel> parseLogLevel(std::string_view s);

}
