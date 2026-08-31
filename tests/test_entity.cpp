#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <engine/entity.hpp>
#include <glm/vec2.hpp>
#include <stdexcept>
#include <vector>

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
    REQUIRE_THROWS_AS(scene.addSpriteAnimation(999, engine::SpriteAnimation::uniform(0, {0}, 0.1f)),
                      std::out_of_range);
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

TEST_CASE("SpriteSheetLayout::grid produces frame rects in row-major order", "[renderer]") {
    const engine::SpriteSheetLayout layout = engine::SpriteSheetLayout::grid({16.f, 16.f}, 2, 2);

    REQUIRE(layout.frames.size() == 4);
    REQUIRE(layout.frames[0].origin == glm::vec2{0.f, 0.f});
    REQUIRE(layout.frames[1].origin == glm::vec2{16.f, 0.f});
    REQUIRE(layout.frames[2].origin == glm::vec2{0.f, 16.f});
    REQUIRE(layout.frames[3].origin == glm::vec2{16.f, 16.f});
    for (const auto& frame : layout.frames) {
        REQUIRE(frame.size == glm::vec2{16.f, 16.f});
    }
}

TEST_CASE("SpriteSheetLayout::grid with rows=1 covers a horizontal strip", "[renderer]") {
    const engine::SpriteSheetLayout layout = engine::SpriteSheetLayout::grid({512.f, 512.f}, 8);

    REQUIRE(layout.frames.size() == 8);
    for (std::size_t i = 0; i < layout.frames.size(); ++i) {
        REQUIRE(layout.frames[i].origin == glm::vec2{static_cast<float>(i) * 512.f, 0.f});
    }
}

TEST_CASE("SpriteSheetLayout::grid truncates to frameCount when set", "[renderer]") {
    const engine::SpriteSheetLayout layout = engine::SpriteSheetLayout::grid({8.f, 8.f}, 3, 2, 5);

    REQUIRE(layout.frames.size() == 5);
}

TEST_CASE("SpriteAnimation::uniform builds one AnimationFrame per index with the same duration",
          "[scene]") {
    const engine::SpriteAnimation anim = engine::SpriteAnimation::uniform(0, {2, 0, 1}, 0.25f);

    REQUIRE(anim.sheet == 0);
    REQUIRE(anim.frames.size() == 3);
    REQUIRE(anim.frames[0].index == 2);
    REQUIRE(anim.frames[1].index == 0);
    REQUIRE(anim.frames[2].index == 1);
    for (const auto& frame : anim.frames) {
        REQUIRE(frame.duration == 0.25f);
    }
    REQUIRE(anim.loop);
    REQUIRE(anim.currentFrame == 0);
}

TEST_CASE("Scene::addSpriteAnimation/getSpriteAnimation/removeSpriteAnimation round-trip",
          "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();

    REQUIRE(scene.getSpriteAnimation(id) == nullptr);

    scene.addSpriteAnimation(id, engine::SpriteAnimation::uniform(0, {0, 1, 2}, 0.1f));
    REQUIRE(scene.getSpriteAnimation(id) != nullptr);
    REQUIRE(scene.getSpriteAnimation(id)->frames.size() == 3);

    scene.removeSpriteAnimation(id);
    REQUIRE(scene.getSpriteAnimation(id) == nullptr);
}

TEST_CASE("advanceAnimations steps currentFrame forward once duration elapses", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addSpriteAnimation(id, engine::SpriteAnimation::uniform(0, {0, 1, 2}, 0.1f));

    engine::advanceAnimations(scene, 0.05f);
    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 0);

    engine::advanceAnimations(scene, 0.06f);
    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 1);
}

TEST_CASE("advanceAnimations respects each frame's own duration", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    engine::SpriteAnimation anim;
    anim.sheet = 0;
    anim.frames = {{0, 0.05f}, {1, 0.2f}};
    scene.addSpriteAnimation(id, anim);

    engine::advanceAnimations(scene, 0.06f);
    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 1);

    engine::advanceAnimations(scene, 0.06f);
    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 1);
}

TEST_CASE("advanceAnimations loops back to frame 0 when loop is true", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addSpriteAnimation(id, engine::SpriteAnimation::uniform(0, {0, 1}, 0.1f));

    engine::advanceAnimations(scene, 0.25f);

    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 0);
}

TEST_CASE("advanceAnimations clamps on the last frame when loop is false", "[scene]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addSpriteAnimation(id, engine::SpriteAnimation::uniform(0, {0, 1}, 0.1f, /*loop=*/false));

    engine::advanceAnimations(scene, 0.25f);

    const engine::SpriteAnimation* result = scene.getSpriteAnimation(id);
    REQUIRE(result->currentFrame == 1);
    REQUIRE(result->elapsed == 0.f);
}
