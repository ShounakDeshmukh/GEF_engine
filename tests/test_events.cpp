#include "networkTestUtil.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <engine/entity.hpp>
#include <engine/events/commonEvents.hpp>
#include <engine/events/eventManager.hpp>
#include <engine/networking/sessionClient.hpp>
#include <engine/networking/sessionServer.hpp>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using engine::Scene;
using engine::networking::Bytes;
using engine::networking::ByteView;
using engine::networking::receivedMessage;
namespace ev = engine::events;

namespace {

struct alpha : ev::simpleEvent {
    int v = 0;
};
struct beta : ev::simpleEvent {
    int v = 0;
};
struct loopEvent : ev::simpleEvent {};
struct orphanEvent : ev::simpleEvent {};

/** A networked game event: an int on the wire, nothing else. */
struct pingEvent : ev::simpleEvent {
    static constexpr std::uint16_t kType = 7;
    int v = 0;

    Bytes encode() const {
        Bytes out(sizeof v);
        std::memcpy(out.data(), &v, sizeof v);
        return out;
    }
    static std::optional<pingEvent> decode(ByteView in) {
        if (in.size() != sizeof(int)) {
            return std::nullopt;
        }
        pingEvent e;
        std::memcpy(&e.v, in.data(), sizeof(int));
        return e;
    }
};

static_assert(ev::networkedEvent<pingEvent>);
static_assert(ev::networkedEvent<ev::deathEvent>);
static_assert(!ev::networkedEvent<alpha>);
static_assert(!ev::networkedEvent<ev::inputEvent>);

std::string joined(const std::vector<std::string>& log) {
    std::string out;
    for (const auto& s : log) {
        out += s + " ";
    }
    return out;
}

receivedMessage message(engine::ClientId from, std::uint16_t type, Bytes payload) {
    return receivedMessage{from, type, 999, std::move(payload)};
}

} // namespace

// ---- ordering ------------------------------------------------------------------------------

TEST_CASE("Events run by tick, then priority, then raise order", "[events]") {
    ev::eventManager events;
    Scene scene;
    std::vector<std::string> log;
    events.subscribe<alpha>(
        [&](const alpha& e, Scene&) { log.push_back("a" + std::to_string(e.v)); });

    events.raise(alpha{{}, 1}, 10, 1);
    events.raise(alpha{{}, 2}, 10, 0);
    events.raise(alpha{{}, 3}, 10, 1);
    events.raise(alpha{{}, 4}, 9, 5);   // earlier tick beats any priority
    events.raise(alpha{{}, 5}, 10, -1); // lower priority value runs first
    events.raise(alpha{{}, 6}, 11, -9); // not due yet

    events.run(scene, 10);
    CHECK(joined(log) == "a4 a5 a2 a1 a3 ");
    CHECK(events.queued() == 1);
    CHECK(events.now() == 10);

    log.clear();
    events.run(scene, 11);
    CHECK(joined(log) == "a6 ");
    CHECK(events.queued() == 0);
}

TEST_CASE("Handlers can raise events for the same tick or later ticks", "[events]") {
    ev::eventManager events;
    Scene scene;
    std::vector<std::string> log;
    events.subscribe<alpha>([&](const alpha& e, Scene&) {
        log.push_back("a" + std::to_string(e.v));
        if (e.v == 1) {
            events.raise(beta{{}, 9}, events.now()); // same tick, same run()
        }
        if (e.v == 2) {
            events.raise(beta{{}, 5}, events.now() + 3); // delayed
        }
    });
    events.subscribe<beta>([&](const beta& e, Scene&) { log.push_back("b" + std::to_string(e.v)); });

    events.raise(alpha{{}, 1}, 10, 1);
    events.raise(alpha{{}, 2}, 10, 0);
    events.raise(alpha{{}, 3}, 10, 1);
    events.run(scene, 10);
    // b9 is priority 0, so it runs before a3 (priority 1) even though it was raised later
    CHECK(joined(log) == "a2 a1 b9 a3 ");

    log.clear();
    events.run(scene, 12);
    CHECK(log.empty());
    events.run(scene, 13);
    CHECK(joined(log) == "b5 ");
}

TEST_CASE("Late events still run on a later run()", "[events]") {
    ev::eventManager events;
    Scene scene;
    int handled = 0;
    events.subscribe<alpha>([&](const alpha&, Scene&) { ++handled; });
    events.raise(alpha{}, 3);
    events.run(scene, 20); // a run that skipped ticks still dispatches overdue events
    CHECK(handled == 1);
}

TEST_CASE("Handlers see the event's fields and the Scene they were run with", "[events]") {
    ev::eventManager events;
    Scene scene;
    const engine::EntityId e = scene.createEntity();
    events.subscribe<ev::spawnEvent>([](const ev::spawnEvent& s, Scene& world) {
        world.transform(s.entity).position = s.position;
    });
    events.raise(ev::spawnEvent{{}, e, 4, {12.f, 34.f}}, 1, 2);
    events.run(scene, 1);
    CHECK(scene.transform(e).position == glm::vec2{12.f, 34.f});
}

// ---- run limits and unhandled events --------------------------------------------------------

TEST_CASE("maxEventsPerRun defers the rest of a raise cycle to the next run", "[events]") {
    ev::eventManager events(100);
    Scene scene;
    int count = 0;
    events.subscribe<loopEvent>([&](const loopEvent&, Scene&) {
        ++count;
        events.raise(loopEvent{}, events.now());
    });

    events.raise(loopEvent{}, 0);
    events.run(scene, 0);
    CHECK(count == 100);
    CHECK(events.queued() == 1);

    events.run(scene, 1);
    CHECK(count == 200);
    CHECK(events.queued() == 1);

    events.clear();
    CHECK(events.queued() == 0);
}

TEST_CASE("maxEventsPerRun keeps deferred events in order", "[events]") {
    ev::eventManager events(3);
    Scene scene;
    std::vector<std::string> log;
    events.subscribe<alpha>(
        [&](const alpha& e, Scene&) { log.push_back("a" + std::to_string(e.v)); });
    for (int i = 0; i < 7; ++i) {
        events.raise(alpha{{}, i}, 0);
    }
    events.run(scene, 0);
    CHECK(joined(log) == "a0 a1 a2 ");
    events.run(scene, 1);
    CHECK(joined(log) == "a0 a1 a2 a3 a4 a5 ");
    events.run(scene, 2);
    CHECK(joined(log) == "a0 a1 a2 a3 a4 a5 a6 ");
    CHECK(events.queued() == 0);
}

TEST_CASE("Events with no handler are dropped", "[events]") {
    ev::eventManager events;
    Scene scene;
    int handled = 0;
    events.subscribe<alpha>([&](const alpha&, Scene&) { ++handled; });
    events.raise(orphanEvent{}, 1);
    events.raise(orphanEvent{}, 1);
    events.raise(alpha{}, 1);
    events.run(scene, 1);
    CHECK(handled == 1);
    CHECK(events.queued() == 0);
}

// ---- network decoding -------------------------------------------------------------------------

TEST_CASE("receive() queues accepted events marked remote at the local tick", "[events][net]") {
    ev::eventManager events;
    Scene scene;
    std::optional<pingEvent> got;
    events.subscribe<pingEvent>([&](const pingEvent& e, Scene&) { got = e; }, true);

    pingEvent out;
    out.v = 42;
    std::deque<receivedMessage> messages;
    messages.push_back(message(3, pingEvent::kType, out.encode()));
    events.receive(std::move(messages), 50);
    REQUIRE(events.queued() == 1);

    events.run(scene, 50);
    REQUIRE(got);
    CHECK(got->v == 42);
    CHECK(got->remote);
    CHECK(got->from == 3);
    CHECK(got->tick == 50); // the sender's tick (999) is not comparable, so it is not used
}

TEST_CASE("Local raises are not marked remote", "[events][net]") {
    ev::eventManager events;
    Scene scene;
    std::optional<pingEvent> got;
    events.subscribe<pingEvent>([&](const pingEvent& e, Scene&) { got = e; }, true);
    events.raise(pingEvent{}, 1);
    events.run(scene, 1);
    REQUIRE(got);
    CHECK_FALSE(got->remote);
    CHECK(got->from == engine::kServerId);
}

TEST_CASE("receive() drops unknown types and malformed payloads", "[events][net]") {
    ev::eventManager events;
    Scene scene;
    int pings = 0;
    int deaths = 0;
    events.subscribe<pingEvent>([&](const pingEvent&, Scene&) { ++pings; }, true);
    events.subscribe<ev::deathEvent>([&](const ev::deathEvent&, Scene&) { ++deaths; }, true);

    Bytes death = ev::deathEvent{{}, 5, 6}.encode();
    Bytes truncated(death.begin(), death.end() - 1);
    Bytes trailing = death;
    trailing.push_back(std::byte{0});

    std::deque<receivedMessage> messages;
    messages.push_back(message(1, 99, {}));                          // unknown type
    messages.push_back(message(1, pingEvent::kType, {}));            // empty
    messages.push_back(message(1, pingEvent::kType, Bytes(3)));      // short
    messages.push_back(message(1, ev::deathEvent::kType, {}));       // empty
    messages.push_back(message(1, ev::deathEvent::kType, truncated));
    messages.push_back(message(1, ev::deathEvent::kType, trailing));
    messages.push_back(message(1, ev::deathEvent::kType, Bytes(64, std::byte{0xAB})));
    messages.push_back(message(1, ev::deathEvent::kType, death));    // the only good one
    events.receive(std::move(messages), 1);

    CHECK(events.queued() == 1);
    events.run(scene, 1);
    CHECK(pings == 0);
    CHECK(deaths == 1);
}

TEST_CASE("Common events round-trip and reject truncated payloads", "[events][net]") {
    SECTION("collisionEvent") {
        const ev::collisionEvent in{{}, 11, 22};
        const Bytes bytes = in.encode();
        auto out = ev::collisionEvent::decode(bytes);
        REQUIRE(out);
        CHECK(out->a == 11);
        CHECK(out->b == 22);
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            CHECK_FALSE(ev::collisionEvent::decode(ByteView{bytes.data(), n}));
        }
    }
    SECTION("deathEvent") {
        const ev::deathEvent in{{}, 7, 9};
        const Bytes bytes = in.encode();
        auto out = ev::deathEvent::decode(bytes);
        REQUIRE(out);
        CHECK(out->entity == 7);
        CHECK(out->cause == 9);
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            CHECK_FALSE(ev::deathEvent::decode(ByteView{bytes.data(), n}));
        }
    }
    SECTION("spawnEvent") {
        const ev::spawnEvent in{{}, 3, 4, {1.5f, -2.5f}};
        const Bytes bytes = in.encode();
        auto out = ev::spawnEvent::decode(bytes);
        REQUIRE(out);
        CHECK(out->entity == 3);
        CHECK(out->owner == 4);
        CHECK(out->position == glm::vec2{1.5f, -2.5f});
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            CHECK_FALSE(ev::spawnEvent::decode(ByteView{bytes.data(), n}));
        }
    }
}

// ---- authority: fromNetwork -------------------------------------------------------------------

TEST_CASE("A networked type subscribed without fromNetwork is not accepted from the network",
          "[events][net]") {
    ev::eventManager events;
    Scene scene;
    int deaths = 0;
    int collisions = 0;
    events.subscribe<ev::deathEvent>([&](const ev::deathEvent&, Scene&) { ++deaths; });
    events.subscribe<ev::collisionEvent>([&](const ev::collisionEvent&, Scene&) { ++collisions; });

    std::deque<receivedMessage> messages;
    messages.push_back(message(2, ev::deathEvent::kType, ev::deathEvent{{}, 1, 0}.encode()));
    messages.push_back(message(2, ev::collisionEvent::kType, ev::collisionEvent{{}, 1, 2}.encode()));
    events.receive(std::move(messages), 5);
    CHECK(events.queued() == 0);
    events.run(scene, 5);
    CHECK(deaths == 0);
    CHECK(collisions == 0);

    // still usable locally
    events.raise(ev::deathEvent{{}, 1, 0}, 6);
    events.raise(ev::collisionEvent{{}, 1, 2}, 6);
    events.run(scene, 6);
    CHECK(deaths == 1);
    CHECK(collisions == 1);
}

TEST_CASE("A server ignores death and collision events forged by a client", "[events][net][session]") {
    // The server-authoritative setup of networked_events_demo: clients may only send requests.
    engine::networking::sessionServer server{"tcp://*:0"};
    server.start();

    ev::eventManager events;
    Scene scene;
    int deaths = 0;
    int collisions = 0;
    std::vector<int> requests;
    events.subscribe<ev::deathEvent>([&](const ev::deathEvent&, Scene&) { ++deaths; });
    events.subscribe<ev::collisionEvent>([&](const ev::collisionEvent&, Scene&) { ++collisions; });
    events.subscribe<pingEvent>([&](const pingEvent& e, Scene&) { requests.push_back(e.v); }, true);

    engine::networking::sessionClient attacker(testutil::localEndpoint(server.port()));
    REQUIRE(attacker.join());
    attacker.start();

    attacker.send(ev::deathEvent::kType, ev::deathEvent{{}, 1, 0}.encode(), 1);
    for (std::uint32_t b = 0; b < 6; ++b) {
        attacker.send(ev::collisionEvent::kType, ev::collisionEvent{{}, 1, b}.encode(), 1);
    }
    pingEvent request;
    request.v = 99;
    attacker.send(pingEvent::kType, request.encode(), 1); // messages are ordered: this is last

    std::int64_t tick = 0;
    REQUIRE(testutil::eventually([&] {
        ++tick;
        events.receive(server.drainMessages(), tick);
        events.run(scene, tick);
        return !requests.empty();
    }));
    CHECK(requests == std::vector<int>{99});
    CHECK(deaths == 0);
    CHECK(collisions == 0);
    CHECK(events.queued() == 0);

    attacker.leave();
}
