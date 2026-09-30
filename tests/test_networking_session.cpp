#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <engine/networking/networking.hpp>
#include <engine/simulationThread.hpp>
#include <future>
#include <map>
#include <set>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace std::chrono_literals;
using namespace engine::networking;
namespace {
Bytes value(unsigned n) {
    return {std::byte(n & 255), std::byte((n >> 8) & 255), std::byte{0}};
}
template <typename Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds limit = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    do {
        if (predicate())
            return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
ClientConfig config(const Server& server, bool peer = false) {
    return {.serverHost = "127.0.0.1",
            .joinPort = server.port(),
            .replyTimeout = 200ms,
            .heartbeat = 30ms,
            .peerToPeer = peer};
}
struct Echo {
    std::atomic<bool> stop{false};
    std::thread worker;
    std::string endpoint;
    explicit Echo(std::string ep = "") : endpoint(std::move(ep)) {
        if (endpoint.empty()) {
#ifdef _WIN32
            endpoint = "tcp://127.0.0.1:0";
#else
            static std::atomic<int> counter{0};
            endpoint = "ipc:///tmp/echo_test_" + std::to_string(::getpid()) + "_" +
                       std::to_string(++counter) + ".ipc";
#endif
        }
        std::promise<std::string> ready;
        auto future = ready.get_future();
        worker = std::thread([this, ready = std::move(ready)]() mutable {
            try {
                responseHandler reply(endpoint);
                reply.setReceiveTimeout(10);
                if (endpoint.rfind("tcp://", 0) == 0)
                    endpoint = "tcp://127.0.0.1:" + std::to_string(reply.port());
                ready.set_value(endpoint);
                reply.run<Bytes, Bytes>([](const Bytes& bytes) { return bytes; },
                                        [&] { return !stop; });
            } catch (...) {
                try {
                    ready.set_exception(std::current_exception());
                } catch (...) {
                }
            }
        });
        try {
            endpoint = future.get();
        } catch (...) {
            stop = true;
            worker.join();
            throw;
        }
    }
    ~Echo() {
        stop = true;
        worker.join();
#ifndef _WIN32
        if (endpoint.rfind("ipc://", 0) == 0)
            ::unlink(endpoint.substr(6).c_str());
#endif
    }
};
} // namespace
TEST_CASE("wrappers receive variable bytes from changing subscriptions", "[networking][wrappers]") {
    publisher first("tcp://127.0.0.1:0"), second("tcp://127.0.0.1:0");
    subscriber sub;
    sub.setReceiveTimeout(10);
    CHECK_FALSE(sub.tryReceive());
    const auto one = "tcp://127.0.0.1:" + std::to_string(first.port());
    const auto two = "tcp://127.0.0.1:" + std::to_string(second.port());
    sub.connect(one);
    sub.connect(two);
    std::set<unsigned> seen;
    REQUIRE(eventually([&] {
        first.publish(value(1));
        second.publish(value(2));
        for (int i = 0; i < 2; ++i)
            if (auto bytes = sub.tryReceive(); bytes && bytes->size() == 3)
                seen.insert(std::to_integer<unsigned>((*bytes)[0]));
        return seen.size() == 2;
    }));
    sub.disconnect(one);
    // Drain messages already queued before disconnect.
    while (sub.tryReceive()) {
    }
    for (int i = 0; i < 5; ++i) {
        first.publish(value(1));
        second.publish(value(2));
        const auto bytes = sub.tryReceive();
        REQUIRE(bytes);
        CHECK(*bytes == value(2));
    }
}
TEST_CASE("request wrapper recovers after timeout and server restart", "[networking][wrappers]") {
    auto server = std::make_unique<Echo>();
    const auto ep = server->endpoint;
    requestHandler client(ep);
    client.setReplyTimeout(50);
    REQUIRE(client.send(value(3)).second);
    server.reset();
    CHECK_FALSE(client.send(value(4)).second);
    server = std::make_unique<Echo>(ep);
    REQUIRE(eventually([&] {
        const auto [bytes, ok] = client.send(value(5));
        return ok && bytes == value(5);
    }));
    CHECK(client.send(std::string("hello")) == "hello");
}
TEST_CASE("string response handler uses the common reply envelope", "[networking][wrappers]") {
    responseHandler server("tcp://127.0.0.1:0");
    server.setReceiveTimeout(10);
    std::thread worker([&] { server.run([](std::string input) { return "reply:" + input; }); });
    struct Guard {
        responseHandler& s;
        std::thread& t;
        ~Guard() {
            s.stop();
            t.join();
        }
    } guard{server, worker};
    requestHandler client("tcp://127.0.0.1:" + std::to_string(server.port()));
    client.setReplyTimeout(200);
    CHECK(client.send(std::string("abc")) == "reply:abc");
}
TEST_CASE("one session API supports relay and direct peers with late joins and pause",
          "[networking][session]") {
    const bool peerMode = GENERATE(false, true);
    CAPTURE(peerMode);
    Server server({.bindAddress = "tcp://127.0.0.1",
                   .joinPort = 0,
                   .clientTimeout = 250ms,
                   .relayPlayers = !peerMode});
    server.publishWorld(value(42), 900);
    server.start();
    Client a(config(server, peerMode)), b(config(server, peerMode));
    REQUIRE(a.join() == 1);
    REQUIRE(b.join() == 2);
    const auto initial = a.drain();
    REQUIRE(std::any_of(initial.begin(), initial.end(), [](const auto& e) {
        return e.kind == ServerEvent::Kind::World && e.tick == 900 && e.payload == value(42);
    }));
    std::set<engine::ClientId> atA, atB, atC;
    const auto collect = [](Client& client, auto& seen) {
        for (const auto& event : client.drain())
            if (event.kind == ServerEvent::Kind::Player && event.payload == value(event.client))
                seen.insert(event.client);
    };
    REQUIRE(eventually([&] {
        a.send(value(1), 10);
        b.send(value(2), 20);
        collect(a, atA);
        collect(b, atB);
        return atA.contains(2) && atB.contains(1);
    }));
    Client c(config(server, peerMode));
    REQUIRE(c.join() == 3);
    REQUIRE(eventually([&] {
        a.send(value(1), 11);
        b.send(value(2), 21);
        c.send(value(3), 31);
        collect(a, atA);
        collect(b, atB);
        collect(c, atC);
        return atA.size() == 2 && atB.size() == 2 && atC.size() == 2;
    }));
    CHECK(server.relayedPlayerCount() == (peerMode ? 0 : 3));
    // No send() calls for longer than the server timeout: only heartbeats run.
    std::this_thread::sleep_for(550ms);
    CHECK(server.connectedClients() == 3);
    server.publishWorld(value(43), 901);
    REQUIRE(eventually([&] {
        for (const auto& event : a.drain())
            if (event.kind == ServerEvent::Kind::World && event.tick == 901 &&
                event.payload == value(43))
                return true;
        return false;
    }));
    b.stop();
    REQUIRE(eventually([&] {
        for (const auto& event : a.drain())
            if (event.kind == ServerEvent::Kind::PlayerLeft && event.client == 2)
                return true;
        return false;
    }));
    CHECK(server.connectedClients() == 2);
    c.stop();
    a.stop();
    server.stop();
    server.stop();
}
TEST_CASE("relay welcome includes cached players and clients progress independently",
          "[networking][session]") {
    Server server({.bindAddress = "tcp://127.0.0.1", .joinPort = 0});
    server.start();
    Client a(config(server)), b(config(server));
    a.start();
    b.start();
    for (unsigned i = 1; i <= 200; ++i)
        a.send(value(i), i);
    for (unsigned i = 1; i <= 20; ++i)
        b.send(value(i), i);
    std::map<engine::ClientId, int> counts;
    REQUIRE(eventually([&] {
        for (const auto& e : server.drain())
            if (e.kind == ClientEvent::Kind::Update)
                ++counts[e.client];
        return counts[1] == 200 && counts[2] == 20;
    }));
    CHECK(a.droppedUpdates() == 0);
    CHECK(b.droppedUpdates() == 0);
    Client late(config(server));
    REQUIRE(late.join() == 3);
    std::map<engine::ClientId, std::int64_t> ticks;
    for (const auto& e : late.drain())
        if (e.kind == ServerEvent::Kind::Player)
            ticks[e.client] = e.tick;
    CHECK(ticks[1] == 200);
    CHECK(ticks[2] == 20);
}
TEST_CASE("silent clients time out and mismatched modes fail the handshake",
          "[networking][session]") {
    Server server({.bindAddress = "tcp://127.0.0.1", .joinPort = 0, .clientTimeout = 100ms});
    server.start();
    requestHandler raw("tcp://127.0.0.1:" + std::to_string(server.port()));
    raw.setReplyTimeout(200);
    auto [reply, ok] = raw.send(encode({MessageType::Join, 0, 0, 0, {}}));
    REQUIRE(ok);
    const auto packet = decode(reply);
    REQUIRE(packet);
    REQUIRE(decodeWelcome(packet->payload));
    REQUIRE(eventually([&] { return server.connectedClients() == 0; }));
    bool left = false;
    for (const auto& e : server.drain())
        left |= e.kind == ClientEvent::Kind::Left;
    CHECK(left);
    Client mismatch(config(server, true));
    CHECK_FALSE(mismatch.join());
}

TEST_CASE("simulation tick speed controls sends and pause only leaves heartbeats",
          "[networking][session]") {
    Server server({.bindAddress = "tcp://127.0.0.1", .joinPort = 0, .clientTimeout = 150ms});
    server.start();
    Client client(config(server));
    client.start();
    std::int64_t clock = 0;
    engine::Timeline realTime(&clock, 60);
    engine::Timeline gameTime(realTime, 60);
    engine::SimulationThread simulation(
        engine::Scene{}, gameTime, [&](const engine::TickContext& ctx) {
            client.send(value(static_cast<unsigned>(ctx.tick)), ctx.tick);
        });
    simulation.advanceFrame();
    std::size_t updates = 0;
    const auto awaitUpdates = [&](std::size_t target) {
        return eventually([&] {
            for (const auto& event : server.drain())
                if (event.kind == ClientEvent::Kind::Update)
                    ++updates;
            return updates == target;
        });
    };
    for (int i = 0; i < 10; ++i) {
        ++clock;
        simulation.advanceFrame();
    }
    REQUIRE(awaitUpdates(10));
    simulation.setSpeed(2.f);
    simulation.advanceFrame();
    for (int i = 0; i < 10; ++i) {
        ++clock;
        simulation.advanceFrame();
    }
    REQUIRE(awaitUpdates(30));
    simulation.setSpeed(0.5f);
    simulation.advanceFrame();
    for (int i = 0; i < 10; ++i) {
        ++clock;
        simulation.advanceFrame();
    }
    REQUIRE(awaitUpdates(35));
    simulation.pause();
    simulation.advanceFrame();
    for (int i = 0; i < 10; ++i) {
        ++clock;
        simulation.advanceFrame();
    }
    std::this_thread::sleep_for(350ms);
    CHECK(server.connectedClients() == 1);
    for (const auto& event : server.drain())
        CHECK(event.kind != ClientEvent::Kind::Update);
    simulation.unpause();
    simulation.advanceFrame();
    for (int i = 0; i < 10; ++i) {
        ++clock;
        simulation.advanceFrame();
    }
    REQUIRE(awaitUpdates(40));
}

TEST_CASE("peer input rejects unknown senders and older sequences", "[networking][session]") {
    Server server({.bindAddress = "tcp://127.0.0.1",
                   .joinPort = 0,
                   .clientTimeout = 5000ms,
                   .relayPlayers = false});
    server.start();
    publisher source("tcp://127.0.0.1:0");
    const auto endpoint = "tcp://127.0.0.1:" + std::to_string(source.port());
    Bytes registration;
    for (unsigned char c : endpoint)
        registration.push_back(std::byte(c));
    requestHandler raw("tcp://127.0.0.1:" + std::to_string(server.port()));
    raw.setReplyTimeout(200);
    const auto [bytes, ok] = raw.send(encode({MessageType::Join, 0, 0, 0, registration}));
    REQUIRE(ok);
    const auto packet = decode(bytes);
    REQUIRE(packet);
    const auto welcome = decodeWelcome(packet->payload);
    REQUIRE(welcome);
    Client client(config(server, true));
    client.start();
    REQUIRE(eventually([&] {
        source.publish(encode({MessageType::PlayerState, welcome->client, 10, 100, value(10)}));
        for (const auto& e : client.drain())
            if (e.kind == ServerEvent::Kind::Player && e.payload == value(10))
                return true;
        return false;
    }));
    for (int i = 0; i < 20; ++i) {
        source.publish(encode({MessageType::PlayerState, welcome->client, 9, 90, value(9)}));
        source.publish(encode({MessageType::PlayerState, 250, 20, 200, value(250)}));
        std::this_thread::sleep_for(5ms);
    }
    for (const auto& e : client.drain())
        CHECK(e.kind != ServerEvent::Kind::Player);
    REQUIRE(eventually([&] {
        source.publish(encode({MessageType::PlayerState, welcome->client, 11, 110, value(11)}));
        for (const auto& e : client.drain())
            if (e.kind == ServerEvent::Kind::Player && e.payload == value(11))
                return true;
        return false;
    }));
}

TEST_CASE("remote scene updates remain visible while the local simulation is paused",
          "[networking][session][pause]") {
    const bool peerMode = GENERATE(false, true);
    CAPTURE(peerMode);
    Server server({.bindAddress = "tcp://127.0.0.1",
                   .joinPort = 0,
                   .clientTimeout = 250ms,
                   .relayPlayers = !peerMode});
    server.start();
    Client local(config(server, peerMode)), remote(config(server, peerMode));
    local.start();
    remote.start();
    engine::Scene scene;
    const auto character = scene.createEntity();
    const auto platform = scene.createEntity();
    const auto otherPlayer = scene.createEntity();
    std::int64_t clock = 0;
    engine::Timeline realTime(&clock, 60);
    engine::Timeline gameTime(realTime, 60);
    unsigned sent = 0;
    engine::SimulationThread sim(std::move(scene), gameTime, [&](const engine::TickContext& ctx) {
        ctx.scene.transform(character).position.x += 1.f;
        local.send(value(++sent), ctx.tick);
    });
    sim.advanceFrame();
    ++clock;
    sim.advanceFrame();
    sim.pause();
    sim.advanceFrame();
    auto frame = sim.takeRenderFrame();
    REQUIRE(frame);
    REQUIRE(frame->status.paused);
    const auto pausedTick = frame->status.tick;
    const auto pausedX = frame->scene.transform(character).position.x;
    const auto pausedSent = sent;

    // Deliver real network events through the same post/Scene/render-frame path as the demo.
    const auto pump = [&] {
        if (auto events = local.drain(); !events.empty()) {
            sim.post([events = std::move(events), platform, otherPlayer](engine::Scene& world,
                                                                         engine::Timeline&) {
                for (const auto& event : events) {
                    if (event.kind == ServerEvent::Kind::World)
                        world.transform(platform).position.x = static_cast<float>(event.tick);
                    else if (event.kind == ServerEvent::Kind::Player)
                        world.transform(otherPlayer).position.x = static_cast<float>(event.tick);
                    else if (event.kind == ServerEvent::Kind::PlayerLeft)
                        world.destroyEntity(otherPlayer);
                }
            });
        }
        ++clock;
        sim.advanceFrame();
        if (auto latest = sim.takeRenderFrame())
            frame = std::move(latest);
    };
    for (const int position : {100, 200}) {
        server.publishWorld(value(position), position);
        REQUIRE(eventually([&] {
            remote.send(value(position + 1), position + 1);
            pump();
            return frame->scene.transform(platform).position.x == position &&
                   frame->scene.transform(otherPlayer).position.x == position + 1;
        }));
        CHECK(frame->status.paused);
        CHECK(frame->status.tick == pausedTick);
        CHECK(frame->scene.transform(character).position.x == pausedX);
        CHECK(sent == pausedSent);
    }
    // Wait longer than the server timeout without running a local tick.
    std::this_thread::sleep_for(350ms);
    CHECK(server.connectedClients() == 2);
    sim.unpause();
    sim.advanceFrame();
    ++clock;
    sim.advanceFrame();
    frame = sim.takeRenderFrame();
    REQUIRE(frame);
    CHECK_FALSE(frame->status.paused);
    CHECK(frame->scene.transform(character).position.x > pausedX);
    CHECK(sent > pausedSent);
}
