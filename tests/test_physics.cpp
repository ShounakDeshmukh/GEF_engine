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

TEST_CASE("PhysicsSystem::isCollision detects overlapping colliders", "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {16.f, 16.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    REQUIRE(physics.isCollision(scene, a, b));
}

TEST_CASE("PhysicsSystem::isCollision reports false for separated colliders",
          "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {100.f, 100.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    REQUIRE_FALSE(physics.isCollision(scene, a, b));
}

TEST_CASE("PhysicsSystem::isCollision treats exactly edge-touching rects as overlapping",
          "[physics][collision]") {
    // SDL_HasRectIntersectionFloat uses a zero epsilon for the float variant, so a
    // shared edge (Amax == Bmin) counts as intersecting, unlike the integer variant.
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {32.f, 0.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    REQUIRE(physics.isCollision(scene, a, b));
}

TEST_CASE("PhysicsSystem::isCollision is false when either entity has no collider",
          "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId withCollider = scene.createEntity();
    scene.transform(withCollider).position = {0.f, 0.f};
    scene.addCollider(withCollider, {.size = {32.f, 32.f}});

    const engine::EntityId withoutCollider = scene.createEntity();
    scene.transform(withoutCollider).position = {0.f, 0.f};

    engine::PhysicsSystem physics(0.f);
    REQUIRE_FALSE(physics.isCollision(scene, withCollider, withoutCollider));
    REQUIRE_FALSE(physics.isCollision(scene, withoutCollider, withCollider));
}

TEST_CASE("PhysicsSystem::GetCollisionOverlap returns the intersection rect",
          "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {16.f, 20.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    const engine::Rect overlap = physics.GetCollisionOverlap(scene, a, b);

    REQUIRE(overlap.origin.x == Catch::Approx(16.f));
    REQUIRE(overlap.origin.y == Catch::Approx(20.f));
    REQUIRE(overlap.size.x == Catch::Approx(16.f));
    REQUIRE(overlap.size.y == Catch::Approx(12.f));
}

TEST_CASE("PhysicsSystem::isCollision scales the collider by transform.scale",
          "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {-40.f, 0.f};
    scene.transform(b).scale = {2.f, 2.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    //  COllider B is scaled to 64x64, so it overlaps A by 8 pixels on the left side.
    REQUIRE(physics.isCollision(scene, a, b));
}

TEST_CASE("PhysicsSystem::GetCollisionOverlap is an empty rect when not overlapping",
          "[physics][collision]") {
    engine::Scene scene;
    const engine::EntityId a = scene.createEntity();
    scene.transform(a).position = {0.f, 0.f};
    scene.addCollider(a, {.size = {32.f, 32.f}});

    const engine::EntityId b = scene.createEntity();
    scene.transform(b).position = {500.f, 500.f};
    scene.addCollider(b, {.size = {32.f, 32.f}});

    engine::PhysicsSystem physics(0.f);
    const engine::Rect overlap = physics.GetCollisionOverlap(scene, a, b);

    REQUIRE(overlap.size.x == Catch::Approx(0.f));
    REQUIRE(overlap.size.y == Catch::Approx(0.f));
}
