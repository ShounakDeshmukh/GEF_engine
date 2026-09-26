#include "engine/simulationThread.hpp"

#include "engine/log.hpp"

#include <deque>
#include <utility>

namespace engine {

SimulationThread::SimulationThread(Scene scene, Timeline& gameTime, TickFn onTick,
                                   SimulationThreadConfig config)
    : scene_(std::move(scene)), gameTime_(gameTime), onTick_(std::move(onTick)), config_(config) {}

SimulationThread::~SimulationThread() {
    stop();
}

void SimulationThread::start() {
    if (running_) {
        log::error("SimulationThread::start called while running; ignoring");
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    {
        std::lock_guard lock(failureMutex_);
        failure_ = nullptr;
    }
    stopRequested_ = false;

    // log.cpp's lazy-init flag is not atomic; initializing here orders it before the sim thread.
    log::debug("SimulationThread starting");
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void SimulationThread::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
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
        stepper_.emplace(gameTime_, config_.maxStepsPerFrame);
    }

    std::deque<Task> tasks = tasks_.drainAll();
    for (Task& task : tasks) {
        task(scene_, gameTime_);
    }
    if (auto keyboard = keyboardSlot_.take()) {
        frameKeyboard_ = *keyboard;
    }

    int steps = 0;
    stepper_->beginFrame();
    while (stepper_->step()) {
        onTick_(TickContext{scene_, frameKeyboard_, lastTickKeyboard_, stepper_->tickIndex(),
                            gameTime_.tickSeconds()});
        lastTickKeyboard_ = frameKeyboard_;
        ++steps;
    }

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
        {
            std::lock_guard lock(failureMutex_);
            failure_ = std::current_exception();
        }
        log::error("SimulationThread stopped: exception on sim thread");
    }
    running_ = false;
}

} // namespace engine
