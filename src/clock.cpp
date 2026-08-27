#include "engine/clock.hpp"

namespace engine {

Clock::Clock() : last_(std::chrono::steady_clock::now()) {}

void Clock::tick() noexcept {
    const auto now = std::chrono::steady_clock::now();
    deltaSeconds_ = std::chrono::duration<float>(now - last_).count();
    last_ = now;
}

float Clock::deltaSeconds() const noexcept {
    return deltaSeconds_;
}

} // namespace engine
