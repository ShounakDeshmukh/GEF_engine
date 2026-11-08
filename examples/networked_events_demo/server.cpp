// networked_events_demo server: headless and authoritative. Clients send moveRequest events;
// the server moves every player, kills players that touch the red hazard, respawns them, and
// replicates the Scene back to everyone.
//
// Usage: networked_events_server [endpoint]      (default tcp://*:5555)
// Stop with Ctrl+C.
//
// Every change goes through the server's eventManager:
//   clientJoined / clientLeft   local events raised from the session's roster changes
//   moveRequest                 received from clients through eventManager::receive()
//   collisionEvent              raised by raiseCollisions() for overlapping colliders
//   deathEvent                  raised when a player collides with the hazard; broadcast
//   spawnEvent                  raised on join and, delayed, after a death; broadcast
// Clients also get playerLeft. Networked events carry NetIds, which every process shares.

#include "demoEvents.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <engine/engine.hpp>
#include <map>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
namespace ev = engine::events;

std::atomic<bool> stopRequested{false};

void onSignal(int) {
    stopRequested = true;
}

class demoServer {
public:
    explicit demoServer(const std::string& endpoint) : session_(endpoint), physics_(0.f) {
        events_.subscribe<clientJoined>(
            [this](const clientJoined& e, engine::Scene& scene) { onJoined(e, scene); });
        events_.subscribe<clientLeft>(
            [this](const clientLeft& e, engine::Scene& scene) { onLeft(e, scene); });
        events_.subscribe<demo::moveRequest>(
            [this](const demo::moveRequest& e, engine::Scene& scene) { onMove(e, scene); });
        events_.subscribe<ev::collisionEvent>(
            [this](const ev::collisionEvent& e, engine::Scene& scene) { onCollision(e, scene); });
        events_.subscribe<ev::deathEvent>(
            [this](const ev::deathEvent& e, engine::Scene& scene) { onDeath(e, scene); });
        events_.subscribe<ev::spawnEvent>(
            [this](const ev::spawnEvent& e, engine::Scene& scene) { onSpawn(e, scene); });
    }

    /** Builds the server-owned entities in scene, before it goes to the SimulationThread. */
    void setUp(engine::Scene& scene) {
        hazard_ = scene.createEntity();
        scene.transform(hazard_).position = {(demo::worldWidth - hazardSize.x) / 2.f,
                                             (demo::worldHeight - hazardSize.y) / 2.f};
        scene.addShape(hazard_, {.size = hazardSize, .color = {200, 40, 40, 255}});
        scene.addCollider(hazard_, {.size = hazardSize});
        hazardNetId_ = session_.replicator().track(hazard_);
    }

    int port() const { return session_.port(); }
    void start() { session_.start(); }
    void stop() { session_.stop(); }

    /** onTick, on the sim thread. */
    void tick(const engine::TickContext& ctx) {
        for (const engine::networking::rosterEvent& r : session_.drainRosterEvents()) {
            if (r.change == engine::networking::RosterChange::Joined) {
                events_.raise(clientJoined{{}, r.client.id}, ctx.tick);
            } else {
                events_.raise(clientLeft{{}, r.client.id}, ctx.tick);
            }
        }
        events_.receive(session_.drainMessages(), ctx.tick);
        ev::raiseCollisions(events_, physics_, ctx.scene, ctx.tick);
        events_.run(ctx.scene, ctx.tick);

        for (const auto& [id, p] : players_) {
            if (!p.alive) {
                continue;
            }
            glm::vec2& position = ctx.scene.transform(p.entity).position;
            position += p.direction * playerSpeed * ctx.dt;
            position.x = std::clamp(position.x, 0.f, demo::worldWidth - demo::playerSize);
            position.y = std::clamp(position.y, 0.f, demo::worldHeight - demo::playerSize);
        }

        session_.applyClientStates(ctx.scene); // clients own nothing; this just clears leavers
        session_.publishScene(ctx.scene, ctx.tick);
    }

private:
    static constexpr float playerSpeed = 360.f;
    static constexpr std::int64_t respawnTicks = 120; // 2 s at 60 ticks per second
    static constexpr glm::vec2 hazardSize{160.f, 160.f};

    struct clientJoined : ev::simpleEvent {
        engine::ClientId id = 0;
    };
    struct clientLeft : ev::simpleEvent {
        engine::ClientId id = 0;
    };

    struct player {
        engine::ClientId owner;
        engine::EntityId entity;
        engine::NetId netId;
        engine::Color color;
        glm::vec2 direction{0.f, 0.f};
        bool alive = false;
    };

    static glm::vec2 spawnPoint(engine::ClientId id) {
        return {60.f + 140.f * static_cast<float>(id % 8), 80.f};
    }

    player* findPlayer(engine::NetId netId) {
        for (auto& [id, p] : players_) {
            if (p.netId == netId) {
                return &p;
            }
        }
        return nullptr;
    }

    void onJoined(const clientJoined& e, engine::Scene& scene) {
        static constexpr engine::Color palette[] = {
            {80, 170, 230, 255}, {240, 200, 60, 255}, {120, 220, 120, 255},
            {200, 120, 230, 255}, {240, 150, 70, 255}, {230, 230, 230, 255}};

        // The newcomer learns about every live player already here.
        for (const auto& [id, p] : players_) {
            if (p.alive) {
                const ev::spawnEvent existing{{}, p.netId, id, scene.transform(p.entity).position};
                session_.send(e.id, ev::spawnEvent::kType, existing.encode(), events_.now());
            }
        }

        // The entity exists from now on, but is invisible and harmless until it spawns.
        const engine::EntityId entity = scene.createEntity();
        const engine::NetId netId = session_.replicator().track(entity);
        players_[e.id] = player{e.id, entity, netId, palette[e.id % std::size(palette)]};
        events_.raise(ev::spawnEvent{{}, netId, e.id, spawnPoint(e.id)}, events_.now());
        engine::log::info("client {} joined; {} players", e.id, players_.size());
    }

    void onLeft(const clientLeft& e, engine::Scene& scene) {
        auto it = players_.find(e.id);
        if (it == players_.end()) {
            return;
        }
        // Untracked entities drop out of the next snapshot, so clients remove their copies.
        session_.replicator().untrack(it->second.entity);
        scene.destroyEntity(it->second.entity);
        players_.erase(it);
        session_.broadcast(demo::playerLeft::kType, demo::playerLeft{{}, e.id}.encode(),
                           events_.now());
        engine::log::info("client {} left; {} players", e.id, players_.size());
    }

    void onMove(const demo::moveRequest& e, engine::Scene&) {
        // e.from is set by receive() from the session, so a client can only move its own player
        auto it = players_.find(e.from);
        if (it == players_.end()) {
            return;
        }
        it->second.direction = {static_cast<float>(std::clamp<int>(e.x, -1, 1)),
                                static_cast<float>(std::clamp<int>(e.y, -1, 1))};
    }

    void onCollision(const ev::collisionEvent& e, engine::Scene&) {
        // raiseCollisions() fills in EntityIds; the death that follows is sent over the
        // network, so it carries NetIds.
        if (e.a != hazard_ && e.b != hazard_) {
            return;
        }
        const engine::EntityId other = e.a == hazard_ ? e.b : e.a;
        for (const auto& [id, p] : players_) {
            if (p.entity == other && p.alive) {
                events_.raise(ev::deathEvent{{}, p.netId, hazardNetId_}, events_.now());
            }
        }
    }

    void onDeath(const ev::deathEvent& e, engine::Scene& scene) {
        player* p = findPlayer(e.entity);
        if (!p || !p->alive) {
            return;
        }
        // Dead players have no Shape or Collider: clients stop drawing them, and they can't
        // collide while they wait to respawn.
        p->alive = false;
        scene.removeShape(p->entity);
        scene.removeCollider(p->entity);
        session_.broadcast(ev::deathEvent::kType, e.encode(), events_.now());
        events_.raise(ev::spawnEvent{{}, p->netId, p->owner, spawnPoint(p->owner)},
                      events_.now() + respawnTicks);
        engine::log::info("player {} died; respawning in {} ticks", p->owner, respawnTicks);
    }

    void onSpawn(const ev::spawnEvent& e, engine::Scene& scene) {
        if (e.remote) {
            return; // only the server decides spawns
        }
        player* p = findPlayer(e.entity);
        if (!p) {
            return; // left before the respawn came due
        }
        p->alive = true;
        scene.transform(p->entity).position = e.position;
        scene.addShape(p->entity, {.size = {demo::playerSize, demo::playerSize}, .color = p->color});
        scene.addCollider(p->entity, {.size = {demo::playerSize, demo::playerSize}});
        session_.broadcast(ev::spawnEvent::kType, e.encode(), events_.now());
    }

    engine::networking::sessionServer session_;
    engine::PhysicsSystem physics_; // only for overlap tests; gravity 0
    ev::eventManager events_;
    std::map<engine::ClientId, player> players_;
    engine::EntityId hazard_ = 0;
    engine::NetId hazardNetId_ = 0;
};

} // namespace

int main(int argc, char* argv[]) {
    engine::log::init();
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const std::string endpoint = argc > 1 ? argv[1] : "tcp://*:5555";
    demoServer server(endpoint);
    server.start();
    engine::log::info("networked_events_server listening on port {}", server.port());

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::Scene scene;
    server.setUp(scene);
    engine::SimulationThread sim(std::move(scene), gameTime,
                                 [&server](const engine::TickContext& ctx) { server.tick(ctx); });
    sim.start();

    while (!stopRequested && !sim.failure()) {
        std::this_thread::sleep_for(100ms);
        sim.takeRenderFrame(); // nothing draws on the server; just drop the copies
    }

    sim.stop();
    server.stop();
    engine::log::info("server stopped");
    return sim.failure() ? 1 : 0;
}
