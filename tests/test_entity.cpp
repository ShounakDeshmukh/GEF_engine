#include <catch2/catch_test_macros.hpp>
#include <engine/entity.hpp>
#include <glm/vec2.hpp>
#include <optional>
#include <stdexcept>

TEST_CASE("World::create returns valid, unique ids", "[world]") {
    engine::World world;
    const engine::EntityId a = world.create();
    const engine::EntityId b = world.create();

    REQUIRE(world.exists(a));
    REQUIRE(world.exists(b));
    REQUIRE(a != b);
}

TEST_CASE("World::create populates a Transform and nothing else", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();

    REQUIRE_NOTHROW(world.transform(id));
    REQUIRE(world.rigidBody(id) == nullptr);
    REQUIRE(world.collider(id) == nullptr);
    REQUIRE(world.shape(id) == nullptr);
}

TEST_CASE("World::destroy removes an entity and all of its components", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();
    world.addRigidBody(id);
    world.addCollider(id, engine::Collider{});
    world.addShape(id, engine::Shape{});

    world.destroy(id);

    REQUIRE_FALSE(world.exists(id));
    REQUIRE(world.rigidBody(id) == nullptr);
    REQUIRE(world.collider(id) == nullptr);
    REQUIRE(world.shape(id) == nullptr);
}

TEST_CASE("World::destroy on a nonexistent id is a silent no-op", "[world]") {
    engine::World world;
    REQUIRE_NOTHROW(world.destroy(12345));
}

TEST_CASE("World::create never reuses an id after destroy", "[world]") {
    engine::World world;
    const engine::EntityId first = world.create();
    world.destroy(first);
    const engine::EntityId second = world.create();

    REQUIRE(first != second);
    REQUIRE_FALSE(world.exists(first));
    REQUIRE(world.exists(second));
}

TEST_CASE("World::removeRigidBody leaves other components intact", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();
    world.addRigidBody(id);
    world.addCollider(id, engine::Collider{});

    world.removeRigidBody(id);

    REQUIRE(world.rigidBody(id) == nullptr);
    REQUIRE(world.collider(id) != nullptr);
    REQUIRE(world.exists(id));
}

TEST_CASE("World::transform on an unknown id throws out_of_range", "[world]") {
    engine::World world;
    REQUIRE_THROWS_AS(world.transform(999), std::out_of_range);
}

TEST_CASE("World::add* on an unknown id throws out_of_range", "[world]") {
    engine::World world;
    REQUIRE_THROWS_AS(world.addRigidBody(999), std::out_of_range);
    REQUIRE_THROWS_AS(world.addCollider(999, engine::Collider{}), std::out_of_range);
    REQUIRE_THROWS_AS(world.addShape(999, engine::Shape{}), std::out_of_range);
}

TEST_CASE("World::rigidBodies iteration reflects live mutation", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();
    world.addRigidBody(id);

    for (auto& entry : world.rigidBodies()) {
        entry.second.velocity = glm::vec2{5.f, -3.f};
    }

    REQUIRE(world.rigidBody(id)->velocity.x == 5.f);
    REQUIRE(world.rigidBody(id)->velocity.y == -3.f);
}

TEST_CASE("RigidBody with nullopt gravity round-trips as nullopt", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();
    world.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = std::nullopt});

    REQUIRE_FALSE(world.rigidBody(id)->gravity.has_value());
}

TEST_CASE("RigidBody with an explicit gravity value round-trips correctly", "[world]") {
    engine::World world;
    const engine::EntityId id = world.create();
    world.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = 250.f});

    REQUIRE(world.rigidBody(id)->gravity.has_value());
    REQUIRE(world.rigidBody(id)->gravity.value() == 250.f);
}
