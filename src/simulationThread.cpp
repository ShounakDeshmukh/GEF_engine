#include "engine/simulationThread.hpp"

#include "engine/log.hpp"

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace engine {

SimulationThread::SimulationThread(Scene scene, Timeline& gameTime, TickFn onTick,
                                   SimulationThreadConfig config)
    : scene_(std::move(scene)), gameTime_(gameTime), onTick_(std::move(onTick)), config_(config) {}

SimulationThread::~SimulationThread() {
    stop();
}

void SimulationThread::addSubsystemThread(std::string name, SubsystemFn fn) {
    if (started_) {
        log::error("SimulationThread::addSubsystemThread called after start; ignoring {}", name);
        return;
    }
    subsystems_.push_back(Subsystem{std::move(name), std::move(fn), std::nullopt, {}});
}

void SimulationThread::start() {
    if (running_) {
        log::error("SimulationThread::start called while running; ignoring");
        return;
    }
    joinAll();
    {
        std::lock_guard lock(failureMutex_);
        failure_ = nullptr;
    }
    stopRequested_ = false;

    // log.cpp's lazy-init flag is not atomic; initializing here orders it before the sim thread.
    log::debug("SimulationThread starting");
    started_ = true;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
    for (Subsystem& subsystem : subsystems_) {
        subsystem.thread = std::thread([this, &subsystem] { subsystemMain(subsystem); });
    }
}

void SimulationThread::stop() {
    stopRequested_ = true;
    joinAll();
    running_ = false;
}

bool SimulationThread::running() const noexcept {
    return running_;
}

bool SimulationThread::advanceFrame() {
    if (running_) {
        log::error("SimulationThread::advanceFrame called while running; ignoring");
        return false;
    }
    // After a failure running_ drops as soon as the sim exits, while subsystems may still be
    // finishing a callback.
    joinAll();
    started_ = true;
    for (Subsystem& subsystem : subsystems_) {
        runSubsystemFrame(subsystem);
    }
    runFrame();
    return true;
}

void SimulationThread::submitKeyboard(const KeyboardState& keyboard) {
    keyboardSlot_.publish(keyboard);
}

void SimulationThread::post(Task task) {
    tasks_.push(std::move(task));
}

void SimulationThread::pause() {
    post([](Scene&, Timeline& time) { time.pause(); });
}

void SimulationThread::unpause() {
    post([](Scene&, Timeline& time) { time.unpause(); });
}

void SimulationThread::togglePause() {
    post([](Scene&, Timeline& time) { time.paused() ? time.unpause() : time.pause(); });
}

void SimulationThread::setSpeed(float multiplier) {
    post([multiplier](Scene&, Timeline& time) { time.setSpeedMultiplier(multiplier); });
}

std::optional<RenderFrame> SimulationThread::takeRenderFrame() {
    return frameSlot_.take();
}

std::exception_ptr SimulationThread::failure() const {
    std::lock_guard lock(failureMutex_);
    return failure_;
}

int SimulationThread::runFrame() {
    const bool firstFrame = !stepper_;
    if (firstFrame) {
        std::shared_lock lock(timelineMutex_);
        stepper_.emplace(gameTime_, config_.maxStepsPerFrame);
    }

    std::deque<Task> tasks = tasks_.drainAll();
    if (!tasks.empty()) {
        std::unique_lock lock(timelineMutex_);
        for (Task& task : tasks) {
            task(scene_, gameTime_);
        }
    }
    if (auto keyboard = keyboardSlot_.take()) {
        frameKeyboard_ = *keyboard;
    }

    float dt = 0.f;
    {
        std::shared_lock lock(timelineMutex_);
        stepper_->beginFrame();
        dt = gameTime_.tickSeconds();
    }
    int steps = 0;
    while (stepper_->step()) {
        onTick_(TickContext{scene_, frameKeyboard_, lastTickKeyboard_, stepper_->tickIndex(), dt});
        lastTickKeyboard_ = frameKeyboard_;
        ++steps;
    }

    // Only this thread writes gameTime_, so its own reads here need no lock.
    if (steps > 0 || !tasks.empty() || firstFrame) {
        ++framesPublished_;
        frameSlot_.publish(
            RenderFrame{scene_, SimStatus{stepper_->tickIndex(), gameTime_.paused(),
                                          gameTime_.speedMultiplier(), stepper_->dropped(),
                                          framesPublished_, steps}});
    }
    return steps;
}

void SimulationThread::threadMain() noexcept {
    try {
        while (!stopRequested_) {
            if (runFrame() == 0) {
                std::this_thread::sleep_for(config_.idleSleep);
            }
        }
    } catch (...) {
        recordFailure(std::current_exception(), "sim");
    }
    running_ = false;
}

int SimulationThread::runSubsystemFrame(Subsystem& subsystem) {
    float dt = 0.f;
    {
        std::shared_lock lock(timelineMutex_);
        if (!subsystem.stepper) {
            subsystem.stepper.emplace(gameTime_, config_.maxStepsPerFrame);
        }
        subsystem.stepper->beginFrame();
        dt = gameTime_.tickSeconds();
    }
    int steps = 0;
    while (subsystem.stepper->step()) {
        subsystem.fn(SubsystemContext{subsystem.stepper->tickIndex(), dt});
        ++steps;
    }
    return steps;
}

void SimulationThread::subsystemMain(Subsystem& subsystem) noexcept {
    try {
        while (!stopRequested_) {
            if (runSubsystemFrame(subsystem) == 0) {
                std::this_thread::sleep_for(config_.idleSleep);
            }
        }
    } catch (...) {
        recordFailure(std::current_exception(), subsystem.name);
    }
}

void SimulationThread::recordFailure(std::exception_ptr error, const std::string& where) {
    {
        std::lock_guard lock(failureMutex_);
        if (!failure_) {
            failure_ = std::move(error);
        }
    }
    log::error("SimulationThread stopped: exception on {} thread", where);
    stopRequested_ = true;
}

void SimulationThread::joinAll() {
    if (thread_.joinable()) {
        thread_.join();
    }
    for (Subsystem& subsystem : subsystems_) {
        if (subsystem.thread.joinable()) {
            subsystem.thread.join();
        }
    }
}

} // namespace engine
