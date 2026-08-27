#include <catch2/catch_test_macros.hpp>
#include <engine/entity.hpp>
#include <glm/vec2.hpp>
#include <optional>
#include <stdexcept>

TEST_CASE("Scene::createEntity returns valid, unique ids", "[scene]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();

    REQUIRE(scene.hasEntity(a));
    REQUIRE(scene.hasEntity(b));
    REQUIRE(a != b);
}

TEST_CASE("Scene::createEntity populates a Transform and nothing else", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();

    REQUIRE_NOTHROW(scene.transform(id));
    REQUIRE(scene.getRigidBody(id) == nullptr);
    REQUIRE(scene.getCollider(id) == nullptr);
    REQUIRE(scene.getShape(id) == nullptr);
}

TEST_CASE("Scene::destroyEntity removes an entity and all of its components", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id);
    scene.addCollider(id, engine::Collider{});
    scene.addShape(id, engine::Shape{});

    scene.destroyEntity(id);

    REQUIRE_FALSE(scene.hasEntity(id));
    REQUIRE(scene.getRigidBody(id) == nullptr);
    REQUIRE(scene.getCollider(id) == nullptr);
    REQUIRE(scene.getShape(id) == nullptr);
}

TEST_CASE("Scene::destroyEntity on a nonexistent id is a silent no-op", "[scene]") {
    engine::Scene scene;
    REQUIRE_NOTHROW(scene.destroyEntity(12345));
}

TEST_CASE("Scene::createEntity never reuses an id after destroy", "[scene]") {
    engine::Scene scene;
    const engine::EntityId first = scene.createEntity();
    scene.destroyEntity(first);
    const engine::EntityId second = scene.createEntity();

    REQUIRE(first != second);
    REQUIRE_FALSE(scene.hasEntity(first));
    REQUIRE(scene.hasEntity(second));
}

TEST_CASE("Scene::removeRigidBody leaves other components intact", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id);
    scene.addCollider(id, engine::Collider{});

    scene.removeRigidBody(id);

    REQUIRE(scene.getRigidBody(id) == nullptr);
    REQUIRE(scene.getCollider(id) != nullptr);
    REQUIRE(scene.hasEntity(id));
}

TEST_CASE("Scene::transform on an unknown id throws out_of_range", "[scene]") {
    engine::Scene scene;
    REQUIRE_THROWS_AS(scene.transform(999), std::out_of_range);
}

TEST_CASE("Scene::add* on an unknown id throws out_of_range", "[scene]") {
    engine::Scene scene;
    REQUIRE_THROWS_AS(scene.addRigidBody(999), std::out_of_range);
    REQUIRE_THROWS_AS(scene.addCollider(999, engine::Collider{}), std::out_of_range);
    REQUIRE_THROWS_AS(scene.addShape(999, engine::Shape{}), std::out_of_range);
}

TEST_CASE("Scene::rigidBodies iteration reflects live mutation", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id);

    for (auto& entry : scene.rigidBodies()) {
        entry.second.velocity = glm::vec2{5.f, -3.f};
    }

    REQUIRE(scene.getRigidBody(id)->velocity.x == 5.f);
    REQUIRE(scene.getRigidBody(id)->velocity.y == -3.f);
}

TEST_CASE("RigidBody with nullopt gravity round-trips as nullopt", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = std::nullopt});

    REQUIRE_FALSE(scene.getRigidBody(id)->gravity.has_value());
}

TEST_CASE("RigidBody with an explicit gravity value round-trips correctly", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addRigidBody(id, engine::RigidBody{.velocity = {}, .gravity = 250.f});

    REQUIRE(scene.getRigidBody(id)->gravity.has_value());
    REQUIRE(scene.getRigidBody(id)->gravity.value() == 250.f);
}
