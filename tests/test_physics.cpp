#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <engine/entity.hpp>
#include <engine/physics.hpp>

TEST_CASE("PhysicsSystem gravity is configurable", "[physics]") {
    engine::PhysicsSystem physics(980.f);
    REQUIRE(physics.gravity() == Catch::Approx(980.f));

    physics.setGravity(400.f);
    REQUIRE(physics.gravity() == Catch::Approx(400.f));
}

TEST_CASE("PhysicsSystem applies velocity without gravity", "[physics]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.transform(id).position = {50.f, 100.f};
    scene.addRigidBody(id, engine::RigidBody{.velocity = {20.f, 40.f}});

    engine::PhysicsSystem physics(0.f);
    physics.step(scene, 0.5f);

    REQUIRE(scene.transform(id).position.x == Catch::Approx(60.f));
    REQUIRE(scene.transform(id).position.y == Catch::Approx(120.f));
    REQUIRE(scene.getRigidBody(id)->velocity.x == Catch::Approx(20.f));
    REQUIRE(scene.getRigidBody(id)->velocity.y == Catch::Approx(40.f));
}

TEST_CASE("PhysicsSystem applies gravity to a rigid body", "[physics]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id);

    engine::PhysicsSystem physics(200.f);
    physics.step(scene, 0.25f);

    REQUIRE(scene.getRigidBody(id)->velocity.y == Catch::Approx(50.f));
    REQUIRE(scene.transform(id).position.y == Catch::Approx(12.5f));
}

TEST_CASE("PhysicsSystem ignores entities without rigid bodies", "[physics]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.transform(id).position = {75.f, 125.f};

    engine::PhysicsSystem physics(200.f);
    physics.step(scene, 1.f);

    REQUIRE(scene.transform(id).position.x == Catch::Approx(75.f));
    REQUIRE(scene.transform(id).position.y == Catch::Approx(125.f));
}
