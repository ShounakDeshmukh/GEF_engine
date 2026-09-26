#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <engine/inputHandler.hpp>
#include <engine/keyboardState.hpp>
#include <engine/threading/latestValue.hpp>
#include <engine/threading/threadSafeQueue.hpp>
#include <thread>
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
