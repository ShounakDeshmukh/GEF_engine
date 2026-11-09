// networked_events_demo client: sends WASD as moveRequest events and draws the server's
// replicated Scene. Run several to see each other's squares; yours has a marker above it,
// white while alive and red while you wait to respawn. Touching the red block kills you.
//
// Usage: networked_events_client [endpoint]      (default tcp://localhost:5555)
//
// Events on the client:
//   inputEvent    local, from raiseInputEvents(); its handler sends moveRequest
//   spawnEvent    from the server; tells this client which square is its own
//   deathEvent    from the server
//   playerLeft    from the server

#include "demoEvents.hpp"

#include <engine/engine.hpp>
#include <map>
#include <optional>
#include <string>

namespace {

namespace SC = engine::SC;
namespace ev = engine::events;

constexpr engine::Color aliveMarker{255, 255, 255, 255};
constexpr engine::Color deadMarker{220, 50, 50, 255};

} // namespace

int main(int argc, char* argv[]) {
    engine::log::init();

    const std::string endpoint = argc > 1 ? argv[1] : "tcp://localhost:5555";
    engine::networking::sessionClient client(endpoint);
    bool joined = false;
    for (int attempt = 0; attempt < 5 && !joined; ++attempt) {
        joined = client.join();
    }
    if (!joined) {
        engine::log::error("could not join {}", endpoint);
        return 1;
    }
    client.start();
    const engine::ClientId self = client.id();
    engine::log::info("joined as client {}", self);

    engine::Window window("networked_events_client " + std::to_string(self),
                          static_cast<int>(demo::worldWidth), static_cast<int>(demo::worldHeight));
    engine::Renderer renderer(window);
    engine::InputHandler input;

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);

    // The only local entity: a marker over this client's own square. The client's replicator
    // manages only the entities it receives, so it leaves this one alone.
    engine::Scene scene;
    const engine::EntityId marker = scene.createEntity();
    scene.transform(marker).position = {-100.f, -100.f};
    scene.addShape(marker, {.size = {16.f, 16.f}, .color = aliveMarker});

    // Captured by onTick: belongs to the sim thread from sim.start() until sim.stop().
    ev::eventManager events;
    std::optional<engine::NetId> mine;
    std::map<engine::NetId, engine::ClientId> owners; // from spawn events, for log messages
    bool up = false, left = false, down = false, right = false;

    events.subscribe<ev::inputEvent>([&](const ev::inputEvent& e, engine::Scene&) {
        switch (e.key) {
        case SC::SDL_SCANCODE_W: up = e.pressed; break;
        case SC::SDL_SCANCODE_A: left = e.pressed; break;
        case SC::SDL_SCANCODE_S: down = e.pressed; break;
        case SC::SDL_SCANCODE_D: right = e.pressed; break;
        default: return;
        }
        demo::moveRequest request;
        request.x = static_cast<std::int8_t>((right ? 1 : 0) - (left ? 1 : 0));
        request.y = static_cast<std::int8_t>((down ? 1 : 0) - (up ? 1 : 0));
        client.send(demo::moveRequest::kType, request.encode(), events.now());
    });

    events.subscribe<ev::spawnEvent>([&](const ev::spawnEvent& e, engine::Scene& world) {
        owners[e.entity] = e.owner;
        if (e.owner == self) {
            mine = e.entity;
            world.getShape(marker)->color = aliveMarker;
        }
        engine::log::info("player {} spawned{}", e.owner, e.owner == self ? " (you)" : "");
    }, /*fromNetwork=*/true);

    events.subscribe<ev::deathEvent>([&](const ev::deathEvent& e, engine::Scene& world) {
        if (mine && e.entity == *mine) {
            world.getShape(marker)->color = deadMarker;
            engine::log::info("you died; respawning soon");
        } else {
            engine::log::info("player {} died", owners.contains(e.entity) ? owners[e.entity] : 0);
        }
    }, /*fromNetwork=*/true);

    events.subscribe<demo::playerLeft>([&](const demo::playerLeft& e, engine::Scene&) {
        engine::log::info("player {} left", e.owner);
    }, /*fromNetwork=*/true);

    auto onTick = [&](const engine::TickContext& ctx) {
        // Snapshot first, so events about an entity find it already in the Scene.
        client.applySnapshot(ctx.scene);
        events.receive(client.drainMessages(), ctx.tick);
        ev::raiseInputEvents(events, ctx.keyboard, ctx.previousKeyboard, ctx.tick);
        events.run(ctx.scene, ctx.tick);

        // The spawn event can arrive before the snapshot that contains the entity.
        glm::vec2 markerAt{-100.f, -100.f};
        if (mine) {
            if (auto entity = client.replicator().entityOf(*mine);
                entity && ctx.scene.hasEntity(*entity)) {
                markerAt = ctx.scene.transform(*entity).position +
                           glm::vec2{(demo::playerSize - 16.f) / 2.f, -24.f};
            }
        }
        ctx.scene.transform(marker).position = markerAt;

        // This client owns nothing, so it asks for the newest snapshot every tick.
        client.requestSnapshot(ctx.tick);
    };

    engine::SimulationThread sim(std::move(scene), gameTime, onTick);
    sim.start();

    std::optional<engine::RenderFrame> current;
    bool wasConnected = true;
    while (!window.shouldClose()) {
        window.pollEvents();
        sim.submitKeyboard(engine::KeyboardState::capture(input));

        if (auto frame = sim.takeRenderFrame()) {
            current = std::move(*frame);
        }
        if (sim.failure()) {
            engine::log::error("simulation failed; exiting");
            break;
        }
        if (wasConnected && !client.connected()) {
            engine::log::warn("lost the server");
        }
        wasConnected = client.connected();

        renderer.clear({25, 25, 35, 255});
        if (current) {
            renderer.drawEntities(current->scene);
        }
        renderer.present();
    }

    sim.stop();
    client.leave();
    return sim.failure() ? 1 : 0;
}
