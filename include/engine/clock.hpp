#pragma once

#include <chrono>

namespace engine {

/** Tracks frame-to-frame elapsed time. */
class Clock {
public:
    Clock();

    /** Updates deltaSeconds() to the time elapsed since the last tick(). */
    void tick() noexcept;
    /** Seconds elapsed between the two most recent tick() calls. */
    float deltaSeconds() const noexcept;

private:
    std::chrono::steady_clock::time_point last_;
    float deltaSeconds_ = 0.f;
};

} // namespace engine
