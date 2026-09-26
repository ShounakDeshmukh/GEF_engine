#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <engine/inputHandler.hpp>
#include <engine/keyboardState.hpp>
#include <engine/simulationThread.hpp>
#include <engine/threading/latestValue.hpp>
#include <engine/threading/threadSafeQueue.hpp>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("LatestValue::take on an empty slot returns nullopt", "[threading]") {
    engine::threading::LatestValue<int> slot;

    REQUIRE_FALSE(slot.take().has_value());
}

TEST_CASE("LatestValue::take returns the published value once", "[threading]") {
    engine::threading::LatestValue<int> slot;

    slot.publish(1);

    REQUIRE(slot.take() == 1);
    REQUIRE_FALSE(slot.take().has_value());
}

TEST_CASE("LatestValue::publish replaces an unconsumed value", "[threading]") {
    engine::threading::LatestValue<int> slot;

    slot.publish(1);
    slot.publish(2);

    REQUIRE(slot.take() == 2);
    REQUIRE(slot.overwritten() == 1);
    REQUIRE(slot.published() == 2);
}

TEST_CASE("LatestValue hands off increasing values between threads", "[threading]") {
    constexpr int count = 100'000;
    engine::threading::LatestValue<int> slot;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            slot.publish(i);
        }
    });

    int last = -1;
    bool increasing = true;
    std::uint64_t takes = 0;
    while (last != count - 1) {
        if (auto value = slot.take()) {
            increasing = increasing && *value > last;
            last = *value;
            ++takes;
        }
    }
    producer.join();

    REQUIRE(increasing);
    REQUIRE(slot.overwritten() + takes == count);
}

TEST_CASE("ThreadSafeQueue pops in FIFO order", "[threading]") {
    engine::threading::ThreadSafeQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    REQUIRE(queue.tryPop() == 1);
    REQUIRE(queue.tryPop() == 2);
    REQUIRE(queue.tryPop() == 3);
    REQUIRE_FALSE(queue.tryPop().has_value());
}

TEST_CASE("ThreadSafeQueue at capacity drops the oldest element", "[threading]") {
    engine::threading::ThreadSafeQueue<int> queue(3);

    for (int i = 0; i < 5; ++i) {
        queue.push(i);
    }

    REQUIRE(queue.drainAll() == std::deque<int>{2, 3, 4});
    REQUIRE(queue.dropped() == 2);
}

TEST_CASE("ThreadSafeQueue::waitPop on an empty queue times out", "[threading]") {
    engine::threading::ThreadSafeQueue<int> queue;

    const auto start = std::chrono::steady_clock::now();
    const auto value = queue.waitPop(50ms);
    const auto waited = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(value.has_value());
    REQUIRE(waited >= 50ms);
}

TEST_CASE("ThreadSafeQueue::waitPop wakes for a push from another thread", "[threading]") {
    engine::threading::ThreadSafeQueue<int> queue;

    std::thread producer([&] {
        std::this_thread::sleep_for(20ms);
        queue.push(7);
    });
    const auto value = queue.waitPop(2000ms);
    producer.join();

    REQUIRE(value == 7);
}

TEST_CASE("ThreadSafeQueue keeps per-producer order with several producers", "[threading]") {
    constexpr int producerCount = 4;
    constexpr int perProducer = 10'000;
    engine::threading::ThreadSafeQueue<std::pair<int, int>> queue;

    std::vector<std::thread> producers;
    for (int id = 0; id < producerCount; ++id) {
        producers.emplace_back([&queue, id] {
            for (int seq = 0; seq < perProducer; ++seq) {
                queue.push({id, seq});
            }
        });
    }

    std::vector<int> nextSeq(producerCount, 0);
    bool ordered = true;
    int received = 0;
    while (received < producerCount * perProducer) {
        if (auto item = queue.waitPop(2000ms)) {
            auto [id, seq] = *item;
            ordered = ordered && seq == nextSeq[id];
            nextSeq[id] = seq + 1;
            ++received;
        } else {
            break;
        }
    }
    for (std::thread& producer : producers) {
        producer.join();
    }

    REQUIRE(received == producerCount * perProducer);
    REQUIRE(ordered);
}

TEST_CASE("KeyboardState defaults to every key released", "[threading][input]") {
    const engine::KeyboardState snapshot;

    REQUIRE_FALSE(snapshot.isKeyPressed(engine::SC::SDL_SCANCODE_W));
}

TEST_CASE("KeyboardState::setKey presses only that key", "[threading][input]") {
    engine::KeyboardState snapshot;

    snapshot.setKey(engine::SC::SDL_SCANCODE_W, true);

    REQUIRE(snapshot.isKeyPressed(engine::SC::SDL_SCANCODE_W));
    REQUIRE_FALSE(snapshot.isKeyPressed(engine::SC::SDL_SCANCODE_A));
}

TEST_CASE("KeyboardState ignores the boundary scancode", "[threading][input]") {
    const auto boundary = static_cast<engine::SC::SDL_Scancode>(engine::SC::SDL_SCANCODE_COUNT);
    engine::KeyboardState snapshot;

    snapshot.setKey(boundary, true);

    REQUIRE_FALSE(snapshot.isKeyPressed(boundary));
    REQUIRE(snapshot == engine::KeyboardState{});
}

TEST_CASE("KeyboardState::justPressed is true only on the edge", "[threading][input]") {
    engine::KeyboardState released;
    engine::KeyboardState pressed;
    pressed.setKey(engine::SC::SDL_SCANCODE_W, true);

    REQUIRE(pressed.justPressed(engine::SC::SDL_SCANCODE_W, released));
    REQUIRE_FALSE(pressed.justPressed(engine::SC::SDL_SCANCODE_W, pressed));
}

TEST_CASE("KeyboardState::capture with no simulated input has every key released",
          "[threading][input]") {
    engine::InputHandler handler;

    REQUIRE(engine::KeyboardState::capture(handler) == engine::KeyboardState{});
}

namespace {

struct TickRecord {
    std::int64_t tick;
    float dt;
};

// Counter-driven clock so frames advance only when a test moves us.
struct ManualClock {
    std::int64_t us = 0;
    engine::Timeline realTime{&us};
    engine::Timeline gameTime{realTime, 60};
    engine::EntityId entity = 0;
    std::vector<TickRecord> ticks;

    engine::Scene makeScene() {
        engine::Scene scene;
        entity = scene.createEntity();
        return scene;
    }

    engine::SimulationThread::TickFn recordTicks() {
        return [this](const engine::TickContext& ctx) {
            ctx.scene.transform(entity).position.x += 1.f;
            ticks.push_back({ctx.tick, ctx.dt});
        };
    }
};

// The stepper starts at the first frame, so tests run one at us = 0 before advancing time.
void runFirstFrame(engine::SimulationThread& sim) {
    sim.advanceFrame();
    sim.takeRenderFrame();
}

template <typename Predicate> bool pollUntil(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

} // namespace

TEST_CASE("SimulationThread publishes a frame on the first frame", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());

    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();

    REQUIRE(frame.has_value());
    REQUIRE(frame->status.stepsLastFrame == 0);
    REQUIRE(frame->status.frame == 1);
}

TEST_CASE("SimulationThread ticks follow the timeline", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());
    runFirstFrame(sim);

    clock.us = 50'000;
    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();

    REQUIRE(clock.ticks.size() == 3);
    for (std::size_t i = 0; i < clock.ticks.size(); ++i) {
        REQUIRE(clock.ticks[i].tick == static_cast<std::int64_t>(i + 1));
        REQUIRE(clock.ticks[i].dt == Catch::Approx(1.f / 60.f));
    }
    REQUIRE(frame.has_value());
    REQUIRE(frame->scene.transform(clock.entity).position.x == 3.f);
}

TEST_CASE("SimulationThread publishes nothing for an idle frame", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());
    runFirstFrame(sim);
    clock.us = 50'000;
    sim.advanceFrame();
    sim.takeRenderFrame();

    sim.advanceFrame();

    REQUIRE_FALSE(sim.takeRenderFrame().has_value());
}

TEST_CASE("SimulationThread::pause freezes ticks until unpause", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());
    runFirstFrame(sim);

    sim.pause();
    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->status.paused);

    clock.us += 1'000'000;
    sim.advanceFrame();
    REQUIRE(clock.ticks.empty());

    sim.unpause();
    sim.advanceFrame();
    clock.us += 16'667;
    sim.advanceFrame();
    REQUIRE(clock.ticks.size() == 1);
}

TEST_CASE("SimulationThread::setSpeed 2 doubles the tick rate", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());
    runFirstFrame(sim);

    sim.setSpeed(2.f);
    sim.advanceFrame();
    clock.us += 25'000;
    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();

    REQUIRE(clock.ticks.size() == 3);
    REQUIRE(frame.has_value());
    REQUIRE(frame->status.speed == Catch::Approx(2.f));
}

TEST_CASE("SimulationThread::setSpeed 0.5 halves the tick rate", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());
    runFirstFrame(sim);

    sim.setSpeed(0.5f);
    sim.advanceFrame();
    clock.us += 100'000;
    sim.advanceFrame();

    REQUIRE(clock.ticks.size() == 3);
}

TEST_CASE("SimulationThread reports a key press only on the first tick of a frame",
          "[threading][simulation]") {
    ManualClock clock;
    std::vector<std::pair<bool, bool>> presses;
    engine::SimulationThread sim(
        clock.makeScene(), clock.gameTime, [&presses](const engine::TickContext& ctx) {
            presses.emplace_back(
                ctx.keyboard.isKeyPressed(engine::SC::SDL_SCANCODE_W),
                ctx.keyboard.justPressed(engine::SC::SDL_SCANCODE_W, ctx.previousKeyboard));
        });
    runFirstFrame(sim);

    engine::KeyboardState keyboard;
    keyboard.setKey(engine::SC::SDL_SCANCODE_W, true);
    sim.submitKeyboard(keyboard);
    clock.us += 50'000;
    sim.advanceFrame();

    const std::vector<std::pair<bool, bool>> expected{{true, true}, {true, false}, {true, false}};
    REQUIRE(presses == expected);
}

TEST_CASE("SimulationThread runs posted tasks in order before ticks", "[threading][simulation]") {
    ManualClock clock;
    std::string order;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime,
                                 [&order](const engine::TickContext&) { order += 't'; });
    runFirstFrame(sim);

    sim.post([&order](engine::Scene&, engine::Timeline&) { order += 'a'; });
    sim.post([&order](engine::Scene&, engine::Timeline&) { order += 'b'; });
    clock.us += 16'667;
    sim.advanceFrame();

    REQUIRE(order == "abt");
}

TEST_CASE("SimulationThread::advanceFrame propagates exceptions", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, [](const engine::TickContext&) {
        throw std::runtime_error("tick failed");
    });
    runFirstFrame(sim);

    clock.us += 16'667;

    REQUIRE_THROWS_AS(sim.advanceFrame(), std::runtime_error);
}

TEST_CASE("SimulationThread does not drop time before its first frame", "[threading][simulation]") {
    ManualClock clock;
    engine::SimulationThread sim(clock.makeScene(), clock.gameTime, clock.recordTicks());

    clock.us = 10'000'000;
    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();

    REQUIRE(frame.has_value());
    REQUIRE(frame->status.dropped == 0);
    REQUIRE(frame->status.stepsLastFrame == 0);
}

TEST_CASE("SimulationThread ticks on its own thread", "[threading][simulation][live]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    std::atomic<std::thread::id> tickThread;
    engine::SimulationThread sim(
        engine::Scene{}, gameTime,
        [&tickThread](const engine::TickContext&) { tickThread = std::this_thread::get_id(); });

    sim.start();
    const bool reached = pollUntil([&sim] {
        const auto frame = sim.takeRenderFrame();
        return frame && frame->status.tick >= 10;
    });
    sim.stop();

    REQUIRE(reached);
    REQUIRE(tickThread.load() != std::this_thread::get_id());
    REQUIRE_FALSE(sim.running());
}

TEST_CASE("SimulationThread::stop is idempotent and the destructor joins",
          "[threading][simulation][live]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    {
        engine::SimulationThread sim(engine::Scene{}, gameTime, [](const engine::TickContext&) {});
        sim.start();
        sim.stop();
        sim.stop();
        REQUIRE_FALSE(sim.running());
    }
    {
        engine::SimulationThread sim(engine::Scene{}, gameTime, [](const engine::TickContext&) {});
        sim.start();
    }
}

TEST_CASE("SimulationThread::start while running is ignored", "[threading][simulation][live]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::SimulationThread sim(engine::Scene{}, gameTime, [](const engine::TickContext&) {});

    sim.start();
    sim.start();

    REQUIRE(sim.running());
    sim.stop();
}

TEST_CASE("SimulationThread::pause while running freezes ticks", "[threading][simulation][live]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::SimulationThread sim(engine::Scene{}, gameTime, [](const engine::TickContext&) {});
    sim.start();

    sim.pause();
    std::int64_t pausedTick = -1;
    const bool paused = pollUntil([&] {
        const auto frame = sim.takeRenderFrame();
        if (frame && frame->status.paused) {
            pausedTick = frame->status.tick;
            return true;
        }
        return false;
    });
    std::this_thread::sleep_for(200ms);
    const auto later = sim.takeRenderFrame();
    sim.stop();

    REQUIRE(paused);
    if (later) {
        REQUIRE(later->status.tick == pausedTick);
    }
}

TEST_CASE("SimulationThread captures an exception from the sim thread",
          "[threading][simulation][live]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::SimulationThread sim(engine::Scene{}, gameTime, [](const engine::TickContext& ctx) {
        if (ctx.tick >= 5) {
            throw std::runtime_error("tick failed");
        }
    });

    sim.start();
    const bool stopped = pollUntil([&sim] { return !sim.running(); });

    REQUIRE(stopped);
    const std::exception_ptr failure = sim.failure();
    REQUIRE(failure != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(failure), std::runtime_error);
    REQUIRE_NOTHROW(sim.stop());
}
