// Engine systems running on a Scene whose storage comes from pools, the way a game that
// calls reserveEntities / reservePool would use them.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <engine/entity.hpp>
#include <engine/physics.hpp>
#include <engine/replay/replayRecorder.hpp>
#include <engine/simulationThread.hpp>
#include <engine/timeline.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct Score {
    int points = 0;
};

/** A Scene with its entities, the built-in physics components, and Score pooled. Returned
 *  by value, so every test also covers moving a pooled Scene. */
engine::Scene pooledScene() {
    engine::Scene scene;
    scene.reserveEntities(16);
    scene.reservePool<engine::RigidBody>(16);
    scene.reservePool<engine::Collider>(16);
    scene.reservePool<engine::Shape>(16);
    scene.reservePool<Score>(4);
    return scene;
}

} // namespace

TEST_CASE("PhysicsSystem steps pooled rigid bodies", "[pool][physics]") {
    engine::Scene scene = pooledScene();
    const engine::EntityId id = scene.createEntity();
    scene.transform(id).position = {50.f, 100.f};
    scene.addRigidBody(id, engine::RigidBody{.velocity = {20.f, 0.f}});

    engine::PhysicsSystem physics(200.f);
    physics.step(scene, 0.25f);

    REQUIRE(scene.rigidBodies().pooled());
    REQUIRE(scene.transform(id).position.x == Catch::Approx(55.f));
    REQUIRE(scene.getRigidBody(id)->velocity.y == Catch::Approx(50.f));
}

TEST_CASE("PhysicsSystem detects collisions between pooled colliders", "[pool][physics]") {
    engine::Scene scene = pooledScene();
    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();
    scene.addCollider(a, {.size = {10.f, 10.f}});
    scene.addCollider(b, {.size = {10.f, 10.f}});
    scene.transform(b).position = {5.f, 5.f};

    engine::PhysicsSystem physics(0.f);
    REQUIRE(scene.colliders().pooled());
    REQUIRE(physics.isCollision(scene, a, b));

    scene.transform(b).position = {50.f, 50.f};
    REQUIRE_FALSE(physics.isCollision(scene, a, b));
}

TEST_CASE("SimulationThread ticks a pooled Scene and publishes pooled copies",
          "[pool][simulation]") {
    std::int64_t us = 0;
    engine::Timeline realTime{&us};
    engine::Timeline gameTime{realTime, 60};

    engine::Scene scene = pooledScene();
    const engine::EntityId player = scene.createEntity();
    scene.add<Score>(player);

    std::vector<const Score*> seenByTicks;
    engine::SimulationThread sim(std::move(scene), gameTime, [&](const engine::TickContext& ctx) {
        Score* score = ctx.scene.get<Score>(player);
        score->points += 1;
        seenByTicks.push_back(score);
    });

    sim.advanceFrame(); // the stepper starts on the first frame
    sim.takeRenderFrame();
    us = 50'000; // three ticks at 60 Hz
    sim.advanceFrame();
    const auto frame = sim.takeRenderFrame();

    REQUIRE(seenByTicks.size() == 3);
    REQUIRE(seenByTicks[0] == seenByTicks[2]); // the live component never moved

    REQUIRE(frame.has_value());
    const engine::Scene& drawn = frame->scene;
    REQUIRE(drawn.get<Score>(player)->points == 3);
    REQUIRE(drawn.get<Score>(player) != seenByTicks[0]); // the frame has its own copy
    REQUIRE(drawn.all<Score>().pooled());
    REQUIRE(drawn.all<Score>().capacity() == 4);
    REQUIRE(drawn.transforms().capacity() == 16);
}

TEST_CASE("Replay records a pooled Scene and plays it back into a pooled Scene", "[pool][replay]") {
    engine::replay::replayRecorder recorder(10, 3);
    engine::Scene live = pooledScene();
    const engine::EntityId mover = live.createEntity();
    live.addShape(mover, engine::Shape{});
    for (std::int64_t t = 0; t < 20; ++t) {
        live.transform(mover).position = {static_cast<float>(t), 0.f};
        recorder.record(live, t);
    }

    engine::Scene replay;
    replay.reserveEntities(4);
    replay.reservePool<engine::Shape>(4);
    REQUIRE(recorder.startPlayback(0));
    while (recorder.step(replay)) {
        REQUIRE(replay.transforms().size() == 1);
        REQUIRE(replay.shapes().size() == 1);
    }

    REQUIRE(recorder.playbackTick() == 19);
    REQUIRE(replay.transforms().begin()->second.position.x == 19.f);
    REQUIRE(replay.transforms().pooled());
}

TEST_CASE("Replay into a Scene with too small an entity pool throws", "[pool][replay]") {
    engine::replay::replayRecorder recorder(10, 3);
    engine::Scene live;
    live.createEntity();
    live.createEntity();
    recorder.record(live, 0);

    engine::Scene replay;
    replay.reserveEntities(1);
    REQUIRE(recorder.startPlayback(0));
    REQUIRE_THROWS_AS(recorder.step(replay), std::length_error);
}
