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

namespace {
struct Health {
    int hp = 0;
};
struct Tag {};
} // namespace

TEST_CASE("Scene::add<T> attaches a game-defined component", "[scene][components]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();

    REQUIRE(scene.add<Health>(id, {5}).hp == 5);
    REQUIRE(scene.get<Health>(id)->hp == 5);
    REQUIRE(scene.has<Health>(id));
    REQUIRE_FALSE(scene.has<Tag>(id));
    REQUIRE_THROWS_AS(scene.add<Health>(999), std::out_of_range);

    scene.remove<Health>(id);
    REQUIRE(scene.get<Health>(id) == nullptr);
}

TEST_CASE("Scene::destroyEntity removes game-defined components", "[scene][components]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.add<Health>(id);
    scene.add<Tag>(id);

    scene.destroyEntity(id);

    REQUIRE(scene.all<Health>().empty());
    REQUIRE(scene.all<Tag>().empty());
}

TEST_CASE("Scene::each visits entities that have every listed component", "[scene][components]") {
    engine::Scene scene;
    const engine::EntityId both = scene.createEntity();
    const engine::EntityId healthOnly = scene.createEntity();
    const engine::EntityId tagOnly = scene.createEntity();
    scene.add<Health>(both, {1});
    scene.add<Tag>(both);
    scene.add<Health>(healthOnly, {2});
    scene.add<Tag>(tagOnly);

    std::vector<engine::EntityId> visited;
    scene.each<Health, Tag>([&](engine::EntityId id, Health&, Tag&) { visited.push_back(id); });
    REQUIRE(visited == std::vector{both});

    scene.each<Health>([](engine::EntityId, Health& health) { health.hp += 10; });
    REQUIRE(scene.get<Health>(both)->hp == 11);
    REQUIRE(scene.get<Health>(healthOnly)->hp == 12);
}

TEST_CASE("Scene copies are deep", "[scene][components]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.add<Health>(id, {5});

    engine::Scene copy = scene;
    copy.get<Health>(id)->hp = 9;
    copy.add<Tag>(id);

    REQUIRE(scene.get<Health>(id)->hp == 5);
    REQUIRE_FALSE(scene.has<Tag>(id));
}

TEST_CASE("Named accessors share the generic storage", "[scene][components]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.addShape(id, engine::Shape{});
    scene.add<engine::Collider>(id, {});

    REQUIRE(scene.get<engine::Shape>(id) == scene.getShape(id));
    REQUIRE(scene.colliders().size() == 1);
}

TEST_CASE("Scene::reservePool caps a component type", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<Health>(2);
    REQUIRE(scene.all<Health>().pooled());
    REQUIRE(scene.all<Health>().capacity() == 2);

    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();
    const engine::EntityId c = scene.createEntity();
    scene.add<Health>(a, {1});
    scene.add<Health>(b, {2});
    REQUIRE_THROWS_AS(scene.add<Health>(c, {3}), std::length_error);
    REQUIRE_FALSE(scene.has<Health>(c));

    scene.add<Health>(a, {5}); // overwriting an existing one needs no new slot
    REQUIRE(scene.get<Health>(a)->hp == 5);

    scene.destroyEntity(b);
    REQUIRE(scene.add<Health>(c, {3}).hp == 3);
}

TEST_CASE("Scene::reservePool works for the built-in components", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<engine::RigidBody>(1);
    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();

    scene.addRigidBody(a);
    REQUIRE(scene.rigidBodies().pooled());
    REQUIRE_THROWS_AS(scene.addRigidBody(b), std::length_error);
}

TEST_CASE("Scene::reservePool refuses a type that already has components", "[scene][pool]") {
    engine::Scene scene;
    const engine::EntityId id = scene.createEntity();
    scene.add<Health>(id);
    REQUIRE_THROWS_AS(scene.reservePool<Health>(4), std::logic_error);

    scene.remove<Health>(id);
    REQUIRE_NOTHROW(scene.reservePool<Health>(4));
    REQUIRE(scene.all<Health>().capacity() == 4);
}

TEST_CASE("Scene::reserveEntities caps the number of live entities", "[scene][pool]") {
    engine::Scene scene;
    scene.reserveEntities(2);
    REQUIRE(scene.transforms().pooled());

    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();
    REQUIRE_THROWS_AS(scene.createEntity(), std::length_error);

    scene.destroyEntity(a);
    const engine::EntityId c = scene.createEntity();
    REQUIRE(scene.hasEntity(b));
    REQUIRE(scene.hasEntity(c));
    REQUIRE(c == b + 1); // the failed create didn't use up an id
}

TEST_CASE("Scene::reserveEntities refuses a Scene that has entities", "[scene][pool]") {
    engine::Scene scene;
    scene.createEntity();
    REQUIRE_THROWS_AS(scene.reserveEntities(4), std::logic_error);
}

TEST_CASE("Scene copies get their own pools of the same size", "[scene][pool]") {
    engine::Scene scene;
    scene.reserveEntities(8);
    scene.reservePool<Health>(4);
    const engine::EntityId id = scene.createEntity();
    scene.add<Health>(id, {5});

    engine::Scene copy = scene;
    REQUIRE(copy.transforms().pooled());
    REQUIRE(copy.transforms().capacity() == 8);
    REQUIRE(copy.all<Health>().capacity() == 4);
    REQUIRE(copy.get<Health>(id) != scene.get<Health>(id));

    copy.get<Health>(id)->hp = 9;
    REQUIRE(scene.get<Health>(id)->hp == 5);
}

TEST_CASE("Scene moves keep pooled components in place", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<Health>(4);
    const engine::EntityId id = scene.createEntity();
    Health* health = &scene.add<Health>(id, {5});

    engine::Scene moved = std::move(scene);
    REQUIRE(moved.get<Health>(id) == health);

    std::vector<engine::EntityId> visited;
    moved.each<Health>([&](engine::EntityId e, Health&) { visited.push_back(e); });
    REQUIRE(visited == std::vector{id});
}

TEST_CASE("Scene::reservePool accepts a type whose store exists but is empty", "[scene][pool]") {
    engine::Scene scene;
    REQUIRE(scene.all<Health>().empty()); // creates an empty heap store
    REQUIRE_FALSE(scene.all<Health>().pooled());

    scene.reservePool<Health>(3);
    REQUIRE(scene.all<Health>().pooled());
    REQUIRE(scene.all<Health>().capacity() == 3);
}

TEST_CASE("Scene::remove<T> gives the slot back to T's pool", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<Health>(1);
    const engine::EntityId a = scene.createEntity();
    const engine::EntityId b = scene.createEntity();

    scene.add<Health>(a, {1});
    REQUIRE(scene.all<Health>().full());
    scene.remove<Health>(a);
    REQUIRE_FALSE(scene.all<Health>().full());
    REQUIRE(scene.add<Health>(b, {2}).hp == 2);
}

TEST_CASE("Scene::destroyEntity frees the entity's slot in every pool", "[scene][pool]") {
    engine::Scene scene;
    scene.reserveEntities(1);
    scene.reservePool<Health>(1);
    scene.reservePool<engine::Shape>(1);

    const engine::EntityId first = scene.createEntity();
    scene.add<Health>(first);
    scene.addShape(first, engine::Shape{});
    scene.destroyEntity(first);

    REQUIRE(scene.transforms().empty());
    REQUIRE(scene.all<Health>().empty());
    REQUIRE(scene.shapes().empty());

    const engine::EntityId second = scene.createEntity();
    REQUIRE_NOTHROW(scene.add<Health>(second));
    REQUIRE_NOTHROW(scene.addShape(second, engine::Shape{}));
}

TEST_CASE("Pooled and heap components mix in one Scene", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<Health>(4); // Tag stays on the heap
    const engine::EntityId both = scene.createEntity();
    const engine::EntityId healthOnly = scene.createEntity();
    scene.add<Health>(both, {1});
    scene.add<Tag>(both);
    scene.add<Health>(healthOnly, {2});

    REQUIRE(scene.all<Health>().pooled());
    REQUIRE_FALSE(scene.all<Tag>().pooled());

    std::vector<engine::EntityId> visited;
    scene.each<Tag, Health>([&](engine::EntityId id, Tag&, Health& h) {
        visited.push_back(id);
        h.hp += 10;
    });
    REQUIRE(visited == std::vector{both});
    REQUIRE(scene.get<Health>(both)->hp == 11);
    REQUIRE(scene.get<Health>(healthOnly)->hp == 2);
}

TEST_CASE("A copy of a full pooled Scene is full too", "[scene][pool]") {
    engine::Scene scene;
    scene.reserveEntities(2);
    scene.createEntity();
    scene.createEntity();

    engine::Scene copy = scene;
    REQUIRE_THROWS_AS(copy.createEntity(), std::length_error);

    // Freeing a slot in the copy doesn't free one in the original.
    copy.destroyEntity(copy.transforms().begin()->first);
    REQUIRE_NOTHROW(copy.createEntity());
    REQUIRE_THROWS_AS(scene.createEntity(), std::length_error);
}

TEST_CASE("Scene copy-assignment takes the source's pools", "[scene][pool]") {
    engine::Scene pooled;
    pooled.reserveEntities(4);
    pooled.reservePool<Health>(2);
    const engine::EntityId id = pooled.createEntity();
    pooled.add<Health>(id, {7});

    engine::Scene target;
    target.createEntity();
    target = pooled;

    REQUIRE(target.transforms().pooled());
    REQUIRE(target.transforms().capacity() == 4);
    REQUIRE(target.all<Health>().capacity() == 2);
    REQUIRE(target.get<Health>(id)->hp == 7);
    REQUIRE(target.transforms().size() == 1);
}

TEST_CASE("Pooled Transforms keep their address while other entities come and go",
          "[scene][pool]") {
    engine::Scene scene;
    scene.reserveEntities(64);
    const engine::EntityId keeper = scene.createEntity();
    engine::Transform* kept = &scene.transform(keeper);
    kept->position = {3.f, 4.f};

    std::vector<engine::EntityId> others;
    for (int i = 0; i < 63; ++i) {
        others.push_back(scene.createEntity());
    }
    for (engine::EntityId e : others) {
        scene.destroyEntity(e);
    }
    for (int i = 0; i < 63; ++i) {
        scene.createEntity();
    }

    REQUIRE(&scene.transform(keeper) == kept);
    REQUIRE(kept->position == glm::vec2{3.f, 4.f});
}

TEST_CASE("advanceAnimations runs on pooled SpriteAnimations", "[scene][pool]") {
    engine::Scene scene;
    scene.reservePool<engine::SpriteAnimation>(2);
    const engine::EntityId id = scene.createEntity();
    scene.addSpriteAnimation(id, engine::SpriteAnimation::uniform(0, {0, 1, 2}, 0.1f));

    engine::advanceAnimations(scene, 0.15f);

    REQUIRE(scene.spriteAnimations().pooled());
    REQUIRE(scene.getSpriteAnimation(id)->currentFrame == 1);
}
