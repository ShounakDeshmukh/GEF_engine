// Game-defined components and the camera. WASD moves the player across a level wider than the
// window; the camera follows. Spawn points and the death zone are hidden entities tagged with
// structs declared here, with no engine changes. Walking into the gap respawns the player.
#include <engine/engine.hpp>

namespace {

struct Player {
    float speed = 500.f;
};
struct SpawnPoint {};
struct DeathZone {};

constexpr engine::Color kGround{120, 160, 90, 255};
constexpr engine::Color kGroundAlt{90, 130, 70, 255};

} // namespace

int main() {
    engine::log::init();

    engine::Window window("components_demo", 1280, 720);
    engine::Renderer renderer(window);
    engine::InputHandler input;
    engine::PhysicsSystem physics(0.f);
    engine::Scene scene;

    // Ground tiles across 4000 px, with a gap at tile 10, so scrolling is visible.
    for (int i = 0; i < 20; ++i) {
        if (i == 10) {
            continue;
        }
        const engine::EntityId tile = scene.createEntity();
        scene.transform(tile).position = {static_cast<float>(i) * 200.f, 500.f};
        scene.addShape(tile, {.size = {200.f, 220.f}, .color = i % 2 ? kGround : kGroundAlt});
    }

    // Hidden objects: a Transform, a tag component, and a Collider where it matters. No Shape,
    // so drawEntities skips them.
    for (const float x : {100.f, 2600.f}) {
        const engine::EntityId spawn = scene.createEntity();
        scene.transform(spawn).position = {x, 436.f};
        scene.add<SpawnPoint>(spawn);
    }
    const engine::EntityId gap = scene.createEntity();
    scene.transform(gap).position = {2000.f, 480.f};
    scene.addCollider(gap, {{200.f, 240.f}});
    scene.add<DeathZone>(gap);

    const engine::EntityId player = scene.createEntity();
    scene.transform(player).position = {100.f, 436.f};
    scene.addShape(player, {.size = {64.f, 64.f}, .color = {230, 80, 60, 255}});
    scene.addCollider(player, {{64.f, 64.f}});
    scene.add<Player>(player);

    // The last spawn point the player has passed, or the first one.
    const auto respawnPoint = [&](float playerX) {
        glm::vec2 best{100.f, 436.f};
        for (const auto& [id, tag] : scene.all<SpawnPoint>()) {
            const glm::vec2 position = scene.transform(id).position;
            if (position.x <= playerX && position.x >= best.x) {
                best = position;
            }
        }
        return best;
    };

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper sim(gameTime);

    while (!window.shouldClose()) {
        window.pollEvents();

        sim.beginFrame();
        while (sim.step()) {
            const float dt = gameTime.tickSeconds();
            glm::vec2& position = scene.transform(player).position;
            const float speed = scene.get<Player>(player)->speed;
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_A)) {
                position.x -= speed * dt;
            }
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_D)) {
                position.x += speed * dt;
            }
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_W)) {
                position.y -= speed * dt;
            }
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_S)) {
                position.y += speed * dt;
            }

            scene.each<DeathZone, engine::Collider>(
                [&](engine::EntityId zone, DeathZone&, engine::Collider&) {
                    if (physics.isCollision(scene, player, zone)) {
                        position = respawnPoint(position.x);
                        engine::log::info("death zone: respawned at x = {}", position.x);
                    }
                });
        }

        // Keep the player 300 px from the left edge; the world scrolls under it.
        renderer.setCamera({scene.transform(player).position.x - 300.f, 0.f});

        renderer.clear({40, 40, 60, 255});
        renderer.drawEntities(scene);
        renderer.fillRect({16.f, 16.f}, {24.f, 24.f}, {255, 255, 255, 255}); // screen-space HUD
        renderer.present();
    }
    return 0;
}
