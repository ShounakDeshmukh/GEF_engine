#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <engine/timeline.hpp>
#include <thread>
#include <vector>

TEST_CASE("Timeline::now counts exactly 60 ticks per source second at 60 ticks per second",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 1'000'000;

    REQUIRE(gameTime.now() == 60);
}

TEST_CASE("Timeline::tickSeconds reports the tick duration in real seconds", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    REQUIRE(gameTime.tickSeconds() == Catch::Approx(1.f / 60.f));
    REQUIRE(realTime.tickSeconds() == Catch::Approx(1e-6f));
}

TEST_CASE("Timeline::setSpeedMultiplier doubles the tick rate and leaves tickSeconds unchanged",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    gameTime.setSpeedMultiplier(2.f);
    micros = 1'000'000;

    REQUIRE(gameTime.now() == 120);
    REQUIRE(gameTime.tickSeconds() == Catch::Approx(1.f / 60.f));
}

TEST_CASE("Timeline::setSpeedMultiplier halves the tick rate at 0.5", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    gameTime.setSpeedMultiplier(0.5f);
    micros = 1'000'000;

    REQUIRE(gameTime.now() == 30);
}

TEST_CASE("Timeline::setSpeedMultiplier converts a multiplier to an exact fraction", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    // 0.1 has no exact float representation, but the stored speed is exactly
    // 1/10, so ten seconds must yield exactly one second of ticks.
    gameTime.setSpeedMultiplier(0.1f);
    micros = 10'000'000;

    REQUIRE(gameTime.now() == 60);
}

TEST_CASE("Timeline::setSpeedMultiplier does not retroactively rescale elapsed ticks",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 1'000'000;
    gameTime.setSpeedMultiplier(2.f);
    micros = 2'000'000;

    // 60 ticks banked at 1x, then 120 more at 2x. Not 240.
    REQUIRE(gameTime.now() == 180);
}

TEST_CASE("Timeline::pause freezes now and unpause resumes without a jump", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 1'000'000;
    gameTime.pause();
    REQUIRE(gameTime.paused());

    micros = 5'000'000;
    REQUIRE(gameTime.now() == 60);

    gameTime.unpause();
    REQUIRE_FALSE(gameTime.paused());
    REQUIRE(gameTime.now() == 60);

    micros = 6'000'000;
    REQUIRE(gameTime.now() == 120);
}

TEST_CASE("Timeline::pause on a source freezes a timeline running on it", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Timeline slowMotion(gameTime, 60);

    micros = 1'000'000;
    REQUIRE(slowMotion.now() == 60);

    gameTime.pause();
    micros = 5'000'000;

    REQUIRE(slowMotion.now() == 60);
}

TEST_CASE("Timeline siblings on one root run independently of each other's pause", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Timeline uiTime(realTime, 60);

    gameTime.pause();
    micros = 1'000'000;

    REQUIRE(gameTime.now() == 0);
    REQUIRE(uiTime.now() == 60);
}

TEST_CASE("Timeline accumulates sub-tick advances without drift over 10000 banks", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    // 16666 us is just under one tick at 60/s, so every iteration leaves a
    // remainder. setSpeedMultiplier forces a bank(), which is the only path that folds
    // the remainder into bankedTicks_.
    for (int i = 0; i < 10'000; ++i) {
        micros += 16'666;
        gameTime.setSpeedMultiplier(1.f);
    }

    // floor(166'660'000 * 3 / 50'000)
    REQUIRE(gameTime.now() == 9999);
}

TEST_CASE("Timeline::setSpeedMultiplier rejects out-of-range multipliers without changing now",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 1'000'000;
    gameTime.setSpeedMultiplier(0.f);
    gameTime.setSpeedMultiplier(-2.f);
    gameTime.setSpeedMultiplier(2e6f);

    REQUIRE(gameTime.now() == 60);

    micros = 2'000'000;
    REQUIRE(gameTime.now() == 120);
}

TEST_CASE("Timeline::setTicksPerSecond rejects non-positive arguments without changing now",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 1'000'000;
    gameTime.setTicksPerSecond(0);
    gameTime.setTicksPerSecond(-60);

    REQUIRE(gameTime.now() == 60);
    REQUIRE(gameTime.tickSeconds() == Catch::Approx(1.f / 60.f));
}

TEST_CASE("Timeline constructed with a non-positive tick rate falls back to the source rate",
          "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline broken(realTime, 0);

    micros = 1'000'000;

    REQUIRE(broken.now() == 1'000'000);
    REQUIRE(broken.tickSeconds() == Catch::Approx(1e-6f));
}

TEST_CASE("Timeline default constructor advances against the real clock", "[timeline]") {
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ~30 ticks at 60/s. Bounds are loose because a loaded runner stretches
    // the sleep, but tight enough that a wrong time unit fails.
    const std::int64_t ticks = gameTime.now();
    REQUIRE(ticks >= 25);
    REQUIRE(ticks <= 60);
}

TEST_CASE("Stepper::step consumes one step per pending tick", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper stepper(gameTime);

    micros = 50'000; // three ticks at 60/s
    stepper.beginFrame();

    int steps = 0;
    while (stepper.step()) {
        ++steps;
    }

    REQUIRE(steps == 3);
    REQUIRE(stepper.tickIndex() == 3);
    REQUIRE(stepper.dropped() == 0);
}

TEST_CASE("Stepper::beginFrame drops backlog beyond maxStepsPerFrame", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper stepper(gameTime, 5);

    micros = 1'666'667; // a 100-tick backlog
    stepper.beginFrame();

    int steps = 0;
    while (stepper.step()) {
        ++steps;
    }

    REQUIRE(steps == 5);
    REQUIRE(stepper.dropped() == 95);
    REQUIRE(stepper.tickIndex() == 100);
}

TEST_CASE("Stepper::tickIndex advances simulated time within one frame", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper stepper(gameTime);

    micros = 83'334; // five ticks arrive at once, as they do after a stall
    stepper.beginFrame();

    std::vector<float> sampled;
    while (stepper.step()) {
        sampled.push_back(static_cast<float>(stepper.tickIndex()) * gameTime.tickSeconds());
    }

    // Every substep must see a distinct instant. Sampling Timeline::now() here
    // instead yields the same value five times, because now() tracks the source
    // and the source does not move while the frame is being drained.
    REQUIRE(sampled.size() == 5);
    for (std::size_t i = 1; i < sampled.size(); ++i) {
        REQUIRE(sampled[i] > sampled[i - 1]);
    }
}

TEST_CASE("Timeline::speedMultiplier reports the speed that was set", "[timeline]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    REQUIRE(gameTime.speedMultiplier() == Catch::Approx(1.f));
    REQUIRE(gameTime.ticksPerSecond() == 60);

    gameTime.setSpeedMultiplier(0.5f);

    REQUIRE(gameTime.speedMultiplier() == Catch::Approx(0.5f));
    REQUIRE(gameTime.tickSeconds() == Catch::Approx(1.f / 60.f));
}

TEST_CASE("Stepper::step stops at the tick sampled by beginFrame", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper stepper(gameTime);

    micros = 33'334; // two ticks
    stepper.beginFrame();
    micros = 1'000'000; // advances mid-frame; must not extend this frame

    int steps = 0;
    while (stepper.step()) {
        ++steps;
    }

    REQUIRE(steps == 2);
}

TEST_CASE("Stepper instances on one Timeline advance independently", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper physics(gameTime);
    engine::Stepper animation(gameTime);

    micros = 50'000;
    physics.beginFrame();
    animation.beginFrame();

    while (physics.step()) {
    }

    REQUIRE(physics.tickIndex() == 3);
    REQUIRE(animation.tickIndex() == 0);

    while (animation.step()) {
    }

    REQUIRE(animation.tickIndex() == 3);
}

TEST_CASE("Stepper starts at the timeline's current tick and drops nothing",
          "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);

    micros = 10'000'000; // 600 ticks elapse before the stepper exists
    engine::Stepper stepper(gameTime);

    REQUIRE(stepper.tickIndex() == 600);

    stepper.beginFrame();

    REQUIRE_FALSE(stepper.step());
    REQUIRE(stepper.dropped() == 0);
}

TEST_CASE("Stepper stops stepping while its Timeline is paused", "[timeline][stepper]") {
    std::int64_t micros = 0;
    engine::Timeline realTime(&micros);
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper stepper(gameTime);

    gameTime.pause();
    micros = 1'000'000;
    stepper.beginFrame();

    REQUIRE_FALSE(stepper.step());
    REQUIRE(stepper.tickIndex() == 0);
}
