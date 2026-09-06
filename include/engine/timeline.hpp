#pragma once

#include <cstdint>

namespace engine {

/** A measure of time, running on another Timeline or on real time.
 *
 *  Time is counted in whole ticks. The tick rate is fixed at construction;
 *  speed sets how fast ticks elapse relative to the source.
 *
 *  Tick indices start at zero per Timeline and are not comparable between
 *  machines; a joining peer needs its start tick seeded explicitly.
 *
 *  @thread_safety Concurrent now() and tickSeconds() calls are safe only while
 *  no thread mutates this Timeline. setSpeedMultiplier(), setTicksPerSecond(),
 *  pause() and unpause() write state that now() reads without synchronisation,
 *  so they must not overlap any other call on it; sequence them at a frame
 *  barrier. */
class Timeline {
public:
    /** Root timeline running on chrono::steady_clock at one tick per microsecond. */
    Timeline();

    /** Root timeline running on an external microsecond counter, for driving
     *  time from a source other than the system clock, such as a recorded
     *  replay. The counter must outlive this Timeline and must never
     *  decrease. */
    explicit Timeline(const std::int64_t* microsecondCounter);

    /** Timeline running on source, ticking ticksPerSecond times per second of
     *  simulated time. Under a source that is slowed or sped up, that is not
     *  the same as a second of real time. source must outlive this Timeline. */
    Timeline(const Timeline& source, std::int64_t ticksPerSecond);

    /** Ticks elapsed on this timeline. Monotonic. */
    std::int64_t now() const noexcept;

    /** Simulation seconds one tick represents. Fixed by the tick rate and
     *  unaffected by speed, here or on any source. This is the step size to
     *  advance physics and animation by; it is not how long a tick takes on a
     *  wall clock, which speed does change. */
    float tickSeconds() const noexcept;

    /** Ticks per second of simulated time. */
    std::int64_t ticksPerSecond() const noexcept;

    /** Speed relative to the source, as set by setSpeedMultiplier(). */
    float speedMultiplier() const noexcept;

    /** Sets speed relative to the source: 1.0 matches the source, 0.5 half,
     *  2.0 double. A no-op, logged as an error, if multiplier is outside
     *  0.000001 to 1000000. */
    void setSpeedMultiplier(float multiplier);

    /** Sets the tick rate. A no-op, logged as an error, if rate is not
     *  positive. */
    void setTicksPerSecond(std::int64_t rate);

    /** Freezes now(). A no-op if already paused. */
    void pause() noexcept;
    /** Resumes now() from its frozen value. A no-op if not paused. */
    void unpause() noexcept;
    bool paused() const noexcept;

private:
    void bank() noexcept;
    void recomputeRate(std::int64_t previousRateDen) noexcept;
    std::int64_t sourceNow() const noexcept;

    const Timeline* source_ = nullptr;       // nullptr = root
    const std::int64_t* counter_ = nullptr;  // root only; nullptr = steady_clock
    std::int64_t sourceStart_ = 0;           // source ticks at the last bank()
    std::int64_t bankedTicks_ = 0;           // our ticks counted before that bank()
    std::int64_t remainder_ = 0;             // part of a tick, in rateDen_ units
    std::int64_t rateNum_ = 1, rateDen_ = 1; // our ticks per source tick
    std::int64_t ticksPerSecond_ = 1'000'000;
    std::int64_t speedNum_ = 1, speedDen_ = 1; // speed as an exact fraction
    bool paused_ = false;
};

/** Turns elapsed Timeline time into a whole number of fixed-size steps.
 *
 *  Each Stepper keeps its own place in the Timeline, so separate systems can
 *  step the same Timeline without interfering.
 *
 *  @thread_safety Not thread-safe. */
class Stepper {
public:
    /** Stepper starting at timeline's current tick. maxStepsPerFrame caps how
     *  many steps one frame may run. timeline must outlive this Stepper. */
    explicit Stepper(const Timeline& timeline, int maxStepsPerFrame = 5);

    /** Reads the timeline once and discards any backlog beyond
     *  maxStepsPerFrame. Call at the top of each frame. */
    void beginFrame() noexcept;
    /** Runs one pending step. False once the frame has no steps left. */
    bool step() noexcept;

    /** The tick just run, once step() has returned true. Absolute on the
     *  Timeline, not a count from construction, so it is stable across pauses
     *  and speed changes and can be used to stamp events. Before the first
     *  step() of a frame it is the previous frame's last tick. */
    std::int64_t tickIndex() const noexcept;
    /** Ticks discarded to the per-frame cap since construction. */
    std::int64_t dropped() const noexcept;

private:
    const Timeline* timeline_;
    std::int64_t consumed_ = 0;
    std::int64_t target_ = 0;
    std::int64_t dropped_ = 0;
    int budget_ = 0;
    int maxSteps_;
};

} // namespace engine
