#include "engine/timeline.hpp"

#include "engine/log.hpp"

#include <chrono>
#include <cmath>
#include <numeric>

namespace engine {
namespace {

// setSpeedMultiplier needs an exact integer fraction, so tick
// counting never touches a float. speedScale is multiplied by a float multiplier and rounded to an
// integer numerator, with speedScale as the denominator.
constexpr std::int64_t speedScale = 1'000'000;
// Bounds the multiplier at 1e-6; below this it rounds to a numerator of zero
constexpr double minSpeedMultiplier = 1.0 / speedScale;
// Bounds multiplier * speedScale at 1e12
constexpr double maxSpeedMultiplier = 1'000'000.0;

struct Elapsed {
    std::int64_t ticks;
    std::int64_t remainder;
};

// elapsed grows without bound as uptime does, so the source ticks are divided down before being
// scaled: elapsed * num would overflow on a long-running fast timeline, whereas the intermediates
// here are bounded by the tick rate and speed.
Elapsed scaleElapsed(std::int64_t elapsed, std::int64_t remainder, std::int64_t num,
                     std::int64_t den) noexcept {
    const std::int64_t part = remainder + (elapsed % den) * num;
    return {(elapsed / den) * num + part / den, part % den};
}

std::int64_t steadyMicroseconds() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

Timeline::Timeline() {
    sourceStart_ = sourceNow();
}

Timeline::Timeline(const std::int64_t* microsecondCounter) : counter_(microsecondCounter) {
    sourceStart_ = sourceNow();
}

Timeline::Timeline(const Timeline& source, std::int64_t ticksPerSecond) : source_(&source) {
    if (ticksPerSecond <= 0) {
        log::error("Timeline tick rate must be positive, got {}; matching the source instead",
                   ticksPerSecond);
        ticksPerSecond = source.ticksPerSecond_;
    }

    ticksPerSecond_ = ticksPerSecond;
    recomputeRate(rateDen_);
    sourceStart_ = sourceNow();
}

std::int64_t Timeline::now() const noexcept {
    if (paused_) {
        return bankedTicks_;
    }
    const Elapsed scaled = scaleElapsed(sourceNow() - sourceStart_, remainder_, rateNum_, rateDen_);
    return bankedTicks_ + scaled.ticks;
}

float Timeline::tickSeconds() const noexcept {
    return 1.f / static_cast<float>(ticksPerSecond_);
}

std::int64_t Timeline::ticksPerSecond() const noexcept {
    return ticksPerSecond_;
}

float Timeline::speedMultiplier() const noexcept {
    return static_cast<float>(speedNum_) / static_cast<float>(speedDen_);
}

void Timeline::setSpeedMultiplier(float multiplier) {
    // Negated so that NaN fails the check too.
    if (!(multiplier >= minSpeedMultiplier) || multiplier > maxSpeedMultiplier) {
        log::error("Timeline speed multiplier must be between {} and {}, got {}; ignoring",
                   minSpeedMultiplier, maxSpeedMultiplier, multiplier);
        return;
    }

    bank();
    const std::int64_t previousRateDen = rateDen_;
    const std::int64_t scaledSpeed = std::llround(static_cast<double>(multiplier) * speedScale);
    const std::int64_t divisor = std::gcd(scaledSpeed, speedScale);

    speedNum_ = scaledSpeed / divisor;
    speedDen_ = speedScale / divisor;
    recomputeRate(previousRateDen);
}

void Timeline::setTicksPerSecond(std::int64_t rate) {
    if (rate <= 0) {
        log::error("Timeline tick rate must be positive, got {}; ignoring", rate);
        return;
    }

    bank();
    const std::int64_t previousRateDen = rateDen_;
    ticksPerSecond_ = rate;
    recomputeRate(previousRateDen);
}

void Timeline::pause() noexcept {
    if (paused_) {
        return;
    }
    bank();
    paused_ = true;
}

void Timeline::unpause() noexcept {
    if (!paused_) {
        return;
    }
    bank();
    paused_ = false;
}

bool Timeline::paused() const noexcept {
    return paused_;
}

void Timeline::bank() noexcept {
    const std::int64_t sourceTicks = sourceNow();
    const std::int64_t elapsed = paused_ ? 0 : sourceTicks - sourceStart_;
    const Elapsed scaled = scaleElapsed(elapsed, remainder_, rateNum_, rateDen_);

    bankedTicks_ += scaled.ticks;
    remainder_ = scaled.remainder;
    sourceStart_ = sourceTicks;
}

void Timeline::recomputeRate(std::int64_t previousRateDen) noexcept {
    const std::int64_t sourceRate = source_ != nullptr ? source_->ticksPerSecond_ : ticksPerSecond_;
    const std::int64_t rawNum = ticksPerSecond_ * speedNum_;
    const std::int64_t rawDen = sourceRate * speedDen_;
    const std::int64_t divisor = std::gcd(rawNum, rawDen);

    rateNum_ = rawNum / divisor;
    rateDen_ = rawDen / divisor;
    remainder_ = remainder_ * rateDen_ / previousRateDen;
}

std::int64_t Timeline::sourceNow() const noexcept {
    if (source_ != nullptr) {
        return source_->now();
    }
    return counter_ != nullptr ? *counter_ : steadyMicroseconds();
}

Stepper::Stepper(const Timeline& timeline, int maxStepsPerFrame)
    : timeline_(&timeline), consumed_(timeline.now()), target_(consumed_),
      maxSteps_(maxStepsPerFrame) {}

void Stepper::beginFrame() noexcept {
    target_ = timeline_->now();
    budget_ = maxSteps_;

    const std::int64_t pending = target_ - consumed_;
    if (pending > maxSteps_) {
        const std::int64_t lost = pending - maxSteps_;
        consumed_ += lost;
        dropped_ += lost;
        log::warn("stepper dropped {} ticks (total {})", lost, dropped_);
    }
}

bool Stepper::step() noexcept {
    if (budget_ == 0 || consumed_ >= target_) {
        return false;
    }

    --budget_;
    ++consumed_;
    return true;
}

std::int64_t Stepper::tickIndex() const noexcept {
    return consumed_;
}

std::int64_t Stepper::dropped() const noexcept {
    return dropped_;
}

} // namespace engine
