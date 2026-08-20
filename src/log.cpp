#include "engine/log.hpp"

#include <spdlog/spdlog.h>

namespace engine::log {

namespace {

void ensureInitialized() {
    static bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }
}

} // namespace

void init() {
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::debug);
}

void debug(const std::string& message) {
    ensureInitialized();
    spdlog::debug(message);
}

void info(const std::string& message) {
    ensureInitialized();
    spdlog::info(message);
}

void warn(const std::string& message) {
    ensureInitialized();
    spdlog::warn(message);
}

void error(const std::string& message) {
    ensureInitialized();
    spdlog::error(message);
}

} // namespace engine::log
