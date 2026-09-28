#pragma once

#include "engine/entity.hpp"
#include "engine/keyboardState.hpp"
#include "engine/threading/latestValue.hpp"
#include "engine/threading/threadSafeQueue.hpp"
#include "engine/timeline.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine {

struct SimulationThreadConfig {
    int maxStepsPerFrame = 5;
    std::chrono::microseconds idleSleep{1000}; // after a frame that ran no ticks
};

struct TickContext {
    Scene& scene;
    const KeyboardState& keyboard;
    const KeyboardState& previousKeyboard; // what the previous tick saw
    std::int64_t tick;
    float dt;
};

struct SubsystemContext {
    std::int64_t tick;
    float dt;
};

struct SimStatus {
    std::int64_t tick = 0; // last tick run
    bool paused = false;
    float speed = 1.f;
    std::int64_t dropped = 0; // ticks lost to maxStepsPerFrame
    std::uint64_t frame = 0;  // frames published so far
    int stepsLastFrame = 0;
};

/** What the simulation publishes for main to draw. */
struct RenderFrame {
    Scene scene;
    SimStatus status;
};

/** Owns a Scene and runs fixed-step ticks on a dedicated thread, or on the caller's thread
 *  through advanceFrame().
 *
 *  While running:
 *  - The Scene is reachable only inside onTick and posted tasks.
 *  - gameTime and every Timeline it derives from belong to the sim thread. Change them through
 *    post() or pause()/unpause()/togglePause()/setSpeed(). The owner may use them again once
 *    stop() returns.
 *  - Anything onTick captures belongs to the sim thread.
 *  - Never call SDL from onTick or tasks: capture a KeyboardState on main and submitKeyboard() it.
 *  - A subsystem thread must never touch the Scene or any Timeline; it exchanges data with the
 *    sim only through LatestValue or ThreadSafeQueue. It steps gameTime independently, so its
 *    last tick can differ from the sim's by up to one frame: tag its output with the tick.
 *  - For networking, push per-tick outbound state from onTick into a ThreadSafeQueue the net
 *    thread drains, so its rate follows Timeline speed. Drain inbound messages at the start of
 *    onTick, or post() them.
 *
 *  @thread_safety submitKeyboard(), post(), pause(), unpause(), togglePause(), setSpeed(),
 *  takeRenderFrame(), failure() and running() are safe from any thread. The constructor,
 *  destructor, addSubsystemThread(), start(), stop() and advanceFrame() must be called from the
 *  owning thread. */
class SimulationThread {
public:
    using TickFn = std::function<void(const TickContext&)>;
    using Task = std::function<void(Scene&, Timeline&)>;
    using SubsystemFn = std::function<void(const SubsystemContext&)>;

    SimulationThread(Scene scene, Timeline& gameTime, TickFn onTick,
                     SimulationThreadConfig config = {});
    ~SimulationThread();
    SimulationThread(const SimulationThread&) = delete;
    SimulationThread& operator=(const SimulationThread&) = delete;
    SimulationThread(SimulationThread&&) = delete;
    SimulationThread& operator=(SimulationThread&&) = delete;

    /** Adds a fixed-step thread on gameTime; subsystems run in registration order. A no-op,
     *  logged as an error, after the first start() or advanceFrame(). */
    void addSubsystemThread(std::string name, SubsystemFn fn);

    /** A no-op, logged as an error, if already running. */
    void start();
    /** Idempotent. */
    void stop();
    bool running() const noexcept;

    /** Runs one frame of every subsystem, then of the sim, on the calling thread; exceptions
     *  propagate. A no-op returning false, logged as an error, while running. */
    bool advanceFrame();

    void submitKeyboard(const KeyboardState& keyboard);
    /** Runs task on the sim thread before the next frame's ticks. */
    void post(Task task);
    void pause();
    void unpause();
    void togglePause();
    void setSpeed(float multiplier);

    std::optional<RenderFrame> takeRenderFrame();
    /** The first exception that stopped the threads, or null. */
    std::exception_ptr failure() const;

private:
    struct Subsystem {
        std::string name;
        SubsystemFn fn;
        std::optional<Stepper> stepper;
        std::thread thread;
    };

    int runFrame();
    int runSubsystemFrame(Subsystem& subsystem);
    void threadMain() noexcept;
    void subsystemMain(Subsystem& subsystem) noexcept;
    void recordFailure(std::exception_ptr error, const std::string& where);
    void joinAll();

    Scene scene_;
    Timeline& gameTime_;
    TickFn onTick_;
    SimulationThreadConfig config_;
    // Created at the first frame so time before it is not dropped as backlog.
    std::optional<Stepper> stepper_;

    threading::LatestValue<KeyboardState> keyboardSlot_;
    threading::ThreadSafeQueue<Task> tasks_;
    threading::LatestValue<RenderFrame> frameSlot_;

    KeyboardState frameKeyboard_;
    KeyboardState lastTickKeyboard_;
    std::uint64_t framesPublished_ = 0;

    // Fixed once started_, so subsystem threads can hold references into it.
    std::vector<Subsystem> subsystems_;
    bool started_ = false;
    // Exclusive for posted tasks; shared for Stepper reads. Never held around onTick or fn.
    std::shared_mutex timelineMutex_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    mutable std::mutex failureMutex_;
    std::exception_ptr failure_;
};

} // namespace engine
