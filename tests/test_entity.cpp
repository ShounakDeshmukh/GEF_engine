#include <catch2/catch_test_macros.hpp>
#include <engine/entity.hpp>
#include <glm/vec2.hpp>
#include <optional>
#include <stdexcept>

TEST_CASE("World::createEntity returns valid, unique ids", "[world]") {
    engine::World world;
    const engine::EntityId a = world.createEntity();
    const engine::EntityId b = world.createEntity();

    REQUIRE(world.hasEntity(a));
    REQUIRE(world.hasEntity(b));
    REQUIRE(a != b);
}

TEST_CASE("World::createEntity populates a Transform and nothing else", "[world]") {
    engine::World world;
    const engine::EntityId id = world.createEntity();

    REQUIRE_NOTHROW(world.transform(id));
    REQUIRE(world.getRigidBody(id) == nullptr);
    REQUIRE(world.getCollider(id) == nullptr);
    REQUIRE(world.getShape(id) == nullptr);
}

TEST_CASE("World::destroyEntity removes an entity and all of its components", "[world]") {
    engine::World world;
    const engine::EntityId id = world.createEntity();
    world.addRigidBody(id);
    world.addCollider(id, engine::Collider{});
    world.addShape(id, engine::Shape{});

    world.destroyEntity(id);

    REQUIRE_FALSE(world.hasEntity(id));
    REQUIRE(world.getRigidBody(id) == nullptr);
    REQUIRE(world.getCollider(id) == nullptr);
    REQUIRE(world.getShape(id) == nullptr);
}

TEST_CASE("World::destroyEntity on a nonexistent id is a silent no-op", "[world]") {
    engine::World world;
    REQUIRE_NOTHROW(world.destroyEntity(12345));
}

TEST_CASE("World::createEntity never reuses an id after destroy", "[world]") {
    engine::World world;
    const engine::EntityId first = world.createEntity();
    world.destroyEntity(first);
    const engine::EntityId second = world.createEntity();

    REQUIRE(first != second);
    REQUIRE_FALSE(world.hasEntity(first));
    REQUIRE(world.hasEntity(second));
}

TEST_CASE("World::removeRigidBody leaves other components intact", "[world]") {
    engine::World world;
    const engine::EntityId id = world.createEntity();
    world.addRigidBody(id);
    world.addCollider(id, engine::Collider{});

    world.removeRigidBody(id);

    REQUIRE(world.getRigidBody(id) == nullptr);
    REQUIRE(world.getCollider(id) != nullptr);
    REQUIRE(world.hasEntity(id));
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
    const engine::EntityId id = world.createEntity();
    world.addRigidBody(id);

    for (auto& entry : world.rigidBodies()) {
        entry.second.velocity = glm::vec2{5.f, -3.f};
    }

    REQUIRE(world.getRigidBody(id)->velocity.x == 5.f);
    REQUIRE(world.getRigidBody(id)->velocity.y == -3.f);
}

TEST_CASE("RigidBody with nullopt gravity round-trips as nullopt", "[world]") {
    engine::World world;
    const engine::EntityId id = world.createEntity();
    world.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = std::nullopt});

    REQUIRE_FALSE(world.getRigidBody(id)->gravity.has_value());
}

TEST_CASE("RigidBody with an explicit gravity value round-trips correctly", "[world]") {
    engine::World world;
    const engine::EntityId id = world.createEntity();
    world.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = 250.f});

    REQUIRE(world.getRigidBody(id)->gravity.has_value());
    REQUIRE(world.getRigidBody(id)->gravity.value() == 250.f);
}
