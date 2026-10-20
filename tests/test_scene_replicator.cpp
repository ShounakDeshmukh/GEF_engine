#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <engine/entity.hpp>
#include <engine/ids.hpp>
#include <engine/networking/sceneReplicator.hpp>

using engine::EntityId;
using engine::kServerId;
using engine::makeNetId;
using engine::networking::Bytes;
using engine::networking::sceneReplicator;

namespace {

EntityId createAt(engine::Scene& scene, float x) {
    const EntityId entity = scene.createEntity();
    scene.transform(entity).position.x = x;
    return entity;
}

} // namespace

TEST_CASE("sceneReplicator round-trips every replicated component", "[net][replicator]") {
    sceneReplicator server(kServerId);
    sceneReplicator client(1);
    engine::Scene a;
    engine::Scene b;
    const EntityId source = a.createEntity();
    a.transform(source) = {{12.f, 34.f}, {2.f, 1.f}};
    a.addRigidBody(source, {{5.f, -6.f}});
    a.addCollider(source, {{64.f, 32.f}});
    engine::Shape shape;
    shape.size = {64.f, 32.f};
    shape.color = {1, 2, 3, 4};
    shape.tiled = true;
    a.addShape(source, shape);
    const auto id = server.track(source);
    server.setExtra(id, Bytes{std::byte{7}});

    REQUIRE(client.apply(b, server.encodeOwned(a), kServerId));

    const auto copy = client.entityOf(id);
    REQUIRE(copy.has_value());
    REQUIRE(b.transform(*copy).position == a.transform(source).position);
    REQUIRE(b.transform(*copy).scale == a.transform(source).scale);
    REQUIRE(b.getRigidBody(*copy)->velocity == a.getRigidBody(source)->velocity);
    REQUIRE(b.getCollider(*copy)->size == a.getCollider(source)->size);
    const engine::Shape* copied = b.getShape(*copy);
    REQUIRE(copied->size == shape.size);
    REQUIRE(copied->color.r == 1);
    REQUIRE(copied->color.g == 2);
    REQUIRE(copied->color.b == 3);
    REQUIRE(copied->color.a == 4);
    REQUIRE(copied->tiled);
    const Bytes* extra = client.extraOf(id);
    REQUIRE(extra != nullptr);
    REQUIRE((*extra == Bytes{std::byte{7}})); // parenthesized: Catch2 cannot print std::byte
}

TEST_CASE("sceneReplicator keeps a bound entity's local texture", "[net][replicator]") {
    sceneReplicator server(kServerId);
    sceneReplicator client(1);
    engine::Scene a;
    engine::Scene b;
    const EntityId source = a.createEntity();
    a.addShape(source, {});
    const auto id = server.track(source);
    const EntityId local = b.createEntity();
    engine::Shape textured;
    textured.texture = 3;
    b.addShape(local, textured);
    client.bind(id, local);

    REQUIRE(client.apply(b, server.encodeOwned(a), kServerId));

    REQUIRE(b.getShape(local)->texture == 3);
}

TEST_CASE("sceneReplicator rejects malformed snapshots without changing the scene",
          "[net][replicator]") {
    sceneReplicator server(kServerId);
    sceneReplicator client(1);
    engine::Scene a;
    engine::Scene b;
    const EntityId source = createAt(a, 12.f);
    const auto id = server.track(source);
    REQUIRE(client.apply(b, server.encodeOwned(a), kServerId));
    const EntityId copy = *client.entityOf(id);
    const auto entityCount = b.transforms().size();
    a.transform(source).position.x = 999.f;
    const Bytes moved = server.encodeOwned(a);

    Bytes truncated = moved;
    truncated.pop_back();
    Bytes wrongVersion = moved;
    wrongVersion.front() = std::byte{0xFF};
    Bytes trailing = moved;
    trailing.push_back(std::byte{0});

    for (const Bytes& malformed : {truncated, wrongVersion, trailing}) {
        REQUIRE_FALSE(client.apply(b, malformed, kServerId));
        REQUIRE(b.transforms().size() == entityCount);
        REQUIRE(b.transform(copy).position.x == 12.f);
    }
}

TEST_CASE("sceneReplicator never overwrites entities it owns", "[net][replicator]") {
    sceneReplicator server(kServerId);
    sceneReplicator client(1);
    engine::Scene a;
    engine::Scene b;
    const EntityId own = createAt(b, 5.f);
    REQUIRE(client.track(own) == makeNetId(1, 0));
    server.bind(makeNetId(1, 0), createAt(a, 99.f));

    REQUIRE(client.apply(b, server.encodeAll(a), kServerId));

    REQUIRE(b.transform(own).position.x == 5.f);
}

TEST_CASE("sceneReplicator despawns entities missing from the next snapshot", "[net][replicator]") {
    sceneReplicator server(kServerId);
    sceneReplicator client(1);
    engine::Scene a;
    engine::Scene b;
    server.track(createAt(a, 1.f));
    const EntityId sourceB = createAt(a, 2.f);
    const auto idB = server.track(sourceB);
    REQUIRE(client.apply(b, server.encodeOwned(a), kServerId));
    const EntityId copyB = *client.entityOf(idB);

    server.untrack(sourceB);
    REQUIRE(client.apply(b, server.encodeOwned(a), kServerId));

    REQUIRE_FALSE(client.entityOf(idB).has_value());
    REQUIRE_FALSE(b.hasEntity(copyB));
}

TEST_CASE("sceneReplicator::dropOwner removes every entity of that owner", "[net][replicator]") {
    sceneReplicator other(2);
    sceneReplicator client(1);
    engine::Scene c;
    engine::Scene b;
    const auto first = other.track(createAt(c, 1.f));
    const auto second = other.track(createAt(c, 2.f));
    REQUIRE(client.apply(b, other.encodeOwned(c), 2));
    const EntityId firstCopy = *client.entityOf(first);
    const EntityId secondCopy = *client.entityOf(second);

    client.dropOwner(b, 2);

    REQUIRE_FALSE(client.entityOf(first).has_value());
    REQUIRE_FALSE(client.entityOf(second).has_value());
    REQUIRE_FALSE(b.hasEntity(firstCopy));
    REQUIRE_FALSE(b.hasEntity(secondCopy));
}

TEST_CASE("sceneReplicator::bind reserves the bound local id", "[net][replicator]") {
    sceneReplicator server(kServerId);
    engine::Scene a;
    server.bind(makeNetId(kServerId, 100), a.createEntity());

    REQUIRE(server.track(a.createEntity()) == makeNetId(kServerId, 101));
}
