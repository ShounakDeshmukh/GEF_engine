#pragma once

#include <format>
#include <string>
#include <utility>

namespace engine::log {

// Initializes the logging backend. Idempotent; safe to call more than once
// or never (debug/info/warn/error self-init on first use).
void init();

void debug(const ::std::string& message);
void info(const ::std::string& message);
void warn(const ::std::string& message);
void error(const ::std::string& message);

template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) {
    debug(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) {
    info(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) {
    warn(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) {
    error(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace engine::log
