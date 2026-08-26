#pragma once

#include <format>
#include <string>
#include <utility>

/** Thin wrapper over spdlog for engine-wide logging. */
namespace engine::log {

/** Initializes the logging backend. Idempotent; safe to call more than once
 *  or never (debug/info/warn/error self-init on first use). */
void init();

/** Logs a message at debug level. */
void debug(const ::std::string& message);
/** Logs a message at info level. */
void info(const ::std::string& message);
/** Logs a message at warn level. */
void warn(const ::std::string& message);
/** Logs a message at error level. */
void error(const ::std::string& message);

/** Logs a std::format-style message at debug level. */
template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) {
    debug(std::format(fmt, std::forward<Args>(args)...));
}

/** Logs a std::format-style message at info level. */
template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) {
    info(std::format(fmt, std::forward<Args>(args)...));
}

/** Logs a std::format-style message at warn level. */
template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) {
    warn(std::format(fmt, std::forward<Args>(args)...));
}

/** Logs a std::format-style message at error level. */
template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) {
    error(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace engine::log
