#include "networkTestUtil.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <engine/entity.hpp>
#include <engine/ids.hpp>
#include <engine/networking/peerSession.hpp>
#include <engine/networking/sessionClient.hpp>
#include <engine/networking/sessionServer.hpp>
#include <functional>
#include <mutex>
#include <networking/sessionProtocol.hpp>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using engine::ClientId;
using engine::EntityId;
using engine::NetId;
using engine::networking::clientStats;
using engine::networking::peerSession;
using engine::networking::receivedMessage;
using engine::networking::RosterChange;
using engine::networking::rosterEvent;
using engine::networking::sceneReplicator;
using engine::networking::sessionClient;
using engine::networking::sessionServer;

namespace {

/** Runs step every period on its own thread while holding a mutex that inspect() also takes,
 *  so a test can read the Scene and replicator the step owns without a data race. */
class simLoop {
public:
    simLoop(std::chrono::milliseconds period, std::function<void()> step)
        : step_(std::move(step)), thread_([this, period] {
              while (!stopping_.load()) {
                  {
                      std::lock_guard lock(mutex_);
                      step_();
                  }
                  std::this_thread::sleep_for(period);
              }
          }) {}
    ~simLoop() { stop(); }
    simLoop(const simLoop&) = delete;
    simLoop& operator=(const simLoop&) = delete;

    void stop() {
        stopping_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    template <typename F> auto inspect(F&& f) {
        std::lock_guard lock(mutex_);
        return f();
    }

private:
    std::mutex mutex_;
    std::atomic<bool> stopping_{false};
    std::function<void()> step_;
    std::thread thread_;
};

/** x of an entity in scene replicated from owner, if there is one. */
std::optional<float> ownedEntityX(const engine::Scene& scene, const sceneReplicator& replicator,
                                  ClientId owner) {
    for (const auto& [entity, transform] : scene.transforms()) {
        if (auto id = replicator.netIdOf(entity); id && engine::ownerOf(*id) == owner) {
            return transform.position.x;
        }
    }
    return std::nullopt;
}

/** A started server whose sim loop moves one server-owned platform +1 in x per iteration. */
struct runningServer {
    runningServer() {
        server.start();
        platform = scene.createEntity();
        platformId = server.replicator().track(platform);
        loop.emplace(5ms, [this] {
            scene.transform(platform).position.x += 1.f;
            server.applyClientStates(scene);
            server.publishScene(scene, ++tick);
        });
    }

    std::string endpoint() const { return testutil::localEndpoint(server.port()); }

    sessionServer server{"tcp://*:0"};
    engine::Scene scene;
    EntityId platform{};
    NetId platformId{};
    std::int64_t tick = 0;
    std::optional<simLoop> loop; // last: stops before the rest is destroyed
};

/** Collects roster events from drain() until one matches change and id. */
bool rosterEventArrives(const std::function<std::deque<rosterEvent>()>& drain, RosterChange change,
                        ClientId id, std::chrono::milliseconds timeout = 2000ms) {
    return testutil::eventually(
        [&] {
            const auto events = drain();
            return std::any_of(events.begin(), events.end(), [&](const rosterEvent& event) {
                return event.change == change && event.client.id == id;
            });
        },
        timeout);
}

std::optional<clientStats> statsFor(const sessionServer& server, ClientId id) {
    for (const auto& stats : server.stats()) {
        if (stats.id == id) {
            return stats;
        }
    }
    return std::nullopt;
}

/** A joined, started peer with its player at x = 100 * id, ticking at 60 Hz on its own thread
 *  and recording every platform x it sees. */
struct runningPeer {
    runningPeer(const std::string& serverEndpoint, NetId platformId) : session(serverEndpoint) {
        REQUIRE(session.join());
        player = scene.createEntity();
        scene.transform(player).position.x = 100.f * static_cast<float>(session.id());
        session.replicator().track(player);
        session.start();
        loop.emplace(16ms, [this, platformId] {
            session.publishScene(scene, ++tick);
            session.applyUpdates(scene);
            if (auto platform = session.replicator().entityOf(platformId)) {
                platformXs.insert(scene.transform(*platform).position.x);
            }
        });
    }

    /** True once this peer's scene holds other's player at x = 100 * other's id. */
    bool sees(ClientId other) {
        return loop->inspect([&] {
            return ownedEntityX(scene, session.replicator(), other) ==
                   100.f * static_cast<float>(other);
        });
    }

    peerSession session;
    engine::Scene scene;
    EntityId player{};
    std::int64_t tick = 0;
    std::set<float> platformXs;
    std::optional<simLoop> loop; // last: stops before the session leaves
};

} // namespace

TEST_CASE("sessionServer hands out ids and every client receives the full roster",
          "[net][session]") {
    runningServer fixture;
    sessionClient c1(fixture.endpoint());
    sessionClient c2(fixture.endpoint());
    sessionClient c3(fixture.endpoint());
    for (auto* client : {&c1, &c2, &c3}) {
        REQUIRE(client->join());
    }
    for (auto* client : {&c1, &c2, &c3}) {
        client->start();
    }

    REQUIRE(std::set<ClientId>{c1.id(), c2.id(), c3.id()} == std::set<ClientId>{1, 2, 3});
    for (auto* client : {&c1, &c2, &c3}) {
        std::deque<rosterEvent> events;
        REQUIRE(testutil::eventually([&] {
            events = client->drainRosterEvents();
            return !events.empty();
        }));
        REQUIRE(events.size() == 3);
        std::set<ClientId> joined;
        for (const auto& event : events) {
            REQUIRE(event.change == RosterChange::Joined);
            joined.insert(event.client.id);
        }
        REQUIRE(joined.contains(client->id()));
    }
}

TEST_CASE("sessionServer relays one client's entities to another in update replies",
          "[net][session]") {
    runningServer fixture;
    sessionClient a(fixture.endpoint());
    sessionClient b(fixture.endpoint());
    REQUIRE(a.join());
    REQUIRE(b.join());
    engine::Scene sceneA;
    engine::Scene sceneB;
    const EntityId player = sceneA.createEntity();
    sceneA.transform(player).position.x = 40.f;
    a.replicator().track(player);
    a.start();
    b.start();
    bool moving = false; // guarded by loopA
    float lastSetX = 40.f;
    std::int64_t tickA = 0;
    simLoop loopA(5ms, [&] {
        if (moving) {
            lastSetX += 1.f;
            sceneA.transform(player).position.x = lastSetX;
        }
        a.submitScene(sceneA, ++tickA);
        a.applySnapshot(sceneA);
    });
    simLoop loopB(5ms, [&] { b.applySnapshot(sceneB); });

    REQUIRE(testutil::eventually([&] {
        return loopB.inspect([&] { return ownedEntityX(sceneB, b.replicator(), a.id()) == 40.f; });
    }));
    const auto statsB = statsFor(fixture.server, b.id());
    REQUIRE(statsB.has_value());
    REQUIRE(statsB->snapshotsSent > 0);

    // the server's copy always lags A's, so any overwrite shows as a mismatch
    loopA.inspect([&] { moving = true; });
    std::this_thread::sleep_for(500ms);
    loopA.inspect([&] {
        REQUIRE(sceneA.hasEntity(player));
        REQUIRE(sceneA.transform(player).position.x == lastSetX);
        REQUIRE(lastSetX > 40.f);
    });
}

TEST_CASE("Session messages arrive reliably and in order both ways", "[net][session]") {
    runningServer fixture;
    sessionClient a(fixture.endpoint());
    REQUIRE(a.join());
    a.start();

    for (std::uint16_t i = 0; i < 50; ++i) {
        a.send(i, {}, i);
    }
    std::vector<receivedMessage> atServer;
    REQUIRE(testutil::eventually([&] {
        for (auto& message : fixture.server.drainMessages()) {
            atServer.push_back(std::move(message));
        }
        return atServer.size() >= 50;
    }));
    REQUIRE(atServer.size() == 50);
    for (std::uint16_t i = 0; i < 50; ++i) {
        REQUIRE(atServer[i].type == i);
        REQUIRE(atServer[i].from == a.id());
    }

    for (std::uint16_t k = 0; k < 20; ++k) {
        fixture.server.send(a.id(), k, {}, k);
    }
    std::vector<receivedMessage> atClient;
    REQUIRE(testutil::eventually([&] {
        for (auto& message : a.drainMessages()) {
            atClient.push_back(std::move(message));
        }
        return atClient.size() >= 20;
    }));
    REQUIRE(atClient.size() == 20);
    for (std::uint16_t k = 0; k < 20; ++k) {
        REQUIRE(atClient[k].type == k);
    }
}

TEST_CASE("A client that leaves is reported and its entities are destroyed", "[net][session]") {
    runningServer fixture;
    sessionClient a(fixture.endpoint());
    REQUIRE(a.join());
    const ClientId aId = a.id();
    engine::Scene sceneA;
    a.replicator().track(sceneA.createEntity());
    a.start();
    a.submitScene(sceneA, 1);
    const auto serverHasEntityOfA = [&] {
        return fixture.loop->inspect([&] {
            return ownedEntityX(fixture.scene, fixture.server.replicator(), aId).has_value();
        });
    };
    REQUIRE(testutil::eventually(serverHasEntityOfA));

    a.leave();

    REQUIRE(rosterEventArrives([&] { return fixture.server.drainRosterEvents(); },
                               RosterChange::Left, aId));
    REQUIRE(testutil::eventually([&] { return !serverHasEntityOfA(); }));
}

TEST_CASE("A silent client times out", "[net][session]") {
    runningServer fixture;
    fixture.server.setClientTimeout(200);
    sessionClient silent(fixture.endpoint());
    REQUIRE(silent.join());

    REQUIRE(rosterEventArrives([&] { return fixture.server.drainRosterEvents(); },
                               RosterChange::Left, silent.id(), 1500ms));
}

TEST_CASE("A paused client's heartbeat keeps it connected", "[net][session]") {
    runningServer fixture;
    fixture.server.setClientTimeout(300);
    sessionClient client(fixture.endpoint());
    client.setHeartbeat(50);
    REQUIRE(client.join());
    client.start();

    std::this_thread::sleep_for(800ms);

    const auto roster = fixture.server.roster();
    REQUIRE(std::any_of(roster.begin(), roster.end(),
                        [&](const auto& info) { return info.id == client.id(); }));
    REQUIRE(client.connected());
}

TEST_CASE("Each client's update rate is independent of the others", "[net][session]") {
    runningServer fixture;
    sessionClient a(fixture.endpoint());
    sessionClient b(fixture.endpoint());
    REQUIRE(a.join());
    REQUIRE(b.join());
    a.start();
    b.start();
    engine::Scene sceneA;
    engine::Scene sceneB;
    std::int64_t tickA = 0;
    std::int64_t tickB = 0;

    const auto start = std::chrono::steady_clock::now();
    auto nextA = start;
    auto nextB = start;
    while (std::chrono::steady_clock::now() - start < 1s) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextA) {
            a.submitScene(sceneA, ++tickA);
            nextA += 8ms;
        }
        if (now >= nextB) {
            b.submitScene(sceneB, ++tickB);
            nextB += 33ms;
        }
        std::this_thread::sleep_for(1ms);
    }

    const auto statsA = statsFor(fixture.server, a.id());
    const auto statsB = statsFor(fixture.server, b.id());
    REQUIRE(statsA.has_value());
    REQUIRE(statsB.has_value());
    REQUIRE(statsA->stateUpdates > 2 * statsB->stateUpdates);
    REQUIRE(statsB->stateUpdates >= 15);
}

TEST_CASE("Clients report disconnected after the server stops", "[net][session]") {
    runningServer fixture;
    sessionClient a(fixture.endpoint());
    sessionClient b(fixture.endpoint());
    for (auto* client : {&a, &b}) {
        client->setReplyTimeout(100); // bounds each failed exchange, and leave() at teardown
        client->setHeartbeat(50);
        REQUIRE(client->join());
        client->start();
    }
    REQUIRE(testutil::eventually([&] { return fixture.server.stats().size() == 2; }));

    fixture.server.stop();

    REQUIRE(testutil::eventually([&] { return !a.connected() && !b.connected(); }, 2500ms));
}

TEST_CASE("joinReply round-trips without a snapshot port", "[net][session]") {
    namespace protocol = engine::networking::protocol;
    protocol::joinReply sent;
    sent.status = protocol::ReplyStatus::Ok;
    sent.id = 7;
    sent.updatePort = 4242;

    protocol::joinReply received;
    REQUIRE(protocol::decode(protocol::encode(sent), received));

    REQUIRE(received.status == sent.status);
    REQUIRE(received.id == sent.id);
    REQUIRE(received.updatePort == sent.updatePort);
}

TEST_CASE("Peers see each other and receive the server platform at tick rate", "[net][peer]") {
    runningServer fixture;
    runningPeer p(fixture.endpoint(), fixture.platformId);
    runningPeer q(fixture.endpoint(), fixture.platformId);

    REQUIRE(testutil::eventually([&] { return p.sees(q.session.id()) && q.sees(p.session.id()); }));

    p.loop->inspect([&] { p.platformXs.clear(); });
    std::this_thread::sleep_for(1s);
    REQUIRE(p.loop->inspect([&] { return p.platformXs.size(); }) >= 20);
}

TEST_CASE("A peer that leaves is dropped by the others", "[net][peer]") {
    runningServer fixture;
    runningPeer p(fixture.endpoint(), fixture.platformId);
    runningPeer q(fixture.endpoint(), fixture.platformId);
    const ClientId qId = q.session.id();
    REQUIRE(testutil::eventually([&] { return p.sees(qId) && q.sees(p.session.id()); }));

    q.loop->stop();
    q.session.leave();

    REQUIRE(testutil::eventually(
        [&] {
            return p.loop->inspect([&] {
                const auto peers = p.session.peers();
                return !ownedEntityX(p.scene, p.session.replicator(), qId) &&
                       std::find(peers.begin(), peers.end(), qId) == peers.end();
            });
        },
        3000ms));
}

TEST_CASE("peerSession::connected follows the server", "[net][peer]") {
    runningServer fixture;
    runningPeer p(fixture.endpoint(), fixture.platformId);

    REQUIRE(testutil::eventually([&] { return p.session.connected(); }));

    fixture.server.stop();

    REQUIRE(testutil::eventually([&] { return !p.session.connected(); }, 2500ms));
}
