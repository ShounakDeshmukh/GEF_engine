#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <engine/engine.hpp>
#include <engine/threading/latestValue.hpp>
#include <engine/threading/threadSafeQueue.hpp>
#include <optional>
#include <thread>

namespace {

using namespace std::chrono_literals;
namespace SC = engine::SC;

struct PlayerState {
    std::int64_t tick;
    float x, y;
};

struct PlatformState {
    std::int64_t tick;
    float x, y;
};

constexpr float spawnX = 200.f;
constexpr float spawnY = 800.f;
constexpr float platformX = 832.f;
constexpr float platformY = 700.f;

engine::EntityId addBlock(engine::Scene& scene, glm::vec2 position, glm::vec2 size,
                          engine::Color color) {
    const engine::EntityId id = scene.createEntity();
    scene.transform(id).position = position;
    scene.addShape(id, {.size = size, .color = color});
    scene.addCollider(id, {.size = size});
    return id;
}

// Pushes player out of other along the shallower overlap axis. True if it landed on top.
bool resolveCollision(engine::Scene& scene, const engine::PhysicsSystem& physics,
                      engine::EntityId player, engine::EntityId other) {
    if (!physics.isCollision(scene, player, other)) {
        return false;
    }
    const engine::Rect overlap = physics.GetCollisionOverlap(scene, player, other);
    glm::vec2& position = scene.transform(player).position;
    glm::vec2& velocity = scene.getRigidBody(player)->velocity;
    const glm::vec2 playerCenter = position + scene.getCollider(player)->size * 0.5f;
    const glm::vec2 otherCenter =
        scene.transform(other).position + scene.getCollider(other)->size * 0.5f;

    if (overlap.size.y <= overlap.size.x) {
        if (playerCenter.y < otherCenter.y) {
            position.y -= overlap.size.y;
            velocity.y = 0.f;
            return true;
        }
        position.y += overlap.size.y;
        velocity.y = std::max(velocity.y, 0.f);
        return false;
    }
    position.x += playerCenter.x < otherCenter.x ? -overlap.size.x : overlap.size.x;
    return false;
}

} // namespace

int main() {
    engine::log::init();

    engine::Window window("multithread_demo", 1920, 1080);
    engine::Renderer renderer(window);
    engine::InputHandler input;
    engine::PhysicsSystem physics(980.f);
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);

    engine::Scene scene;
    const engine::EntityId floor =
        addBlock(scene, {0.f, 1000.f}, {1920.f, 80.f}, {60, 170, 80, 255});
    const engine::EntityId platform =
        addBlock(scene, {platformX, platformY}, {256.f, 32.f}, {230, 140, 40, 255});
    const engine::EntityId player =
        addBlock(scene, {spawnX, spawnY}, {64.f, 64.f}, {240, 240, 255, 255});
    scene.addRigidBody(player);

    engine::threading::LatestValue<PlatformState> platformSlot;
    engine::threading::ThreadSafeQueue<PlayerState> playerStateQueue(256);
    PlatformState lastPlatform{0, platformX, platformY};
    bool grounded = false;

    auto onTick = [&](const engine::TickContext& ctx) {
        engine::Scene& world = ctx.scene;
        if (auto latest = platformSlot.take()) {
            lastPlatform = *latest;
        }
        world.transform(platform).position = {lastPlatform.x, lastPlatform.y};

        glm::vec2& velocity = world.getRigidBody(player)->velocity;
        const bool left = ctx.keyboard.isKeyPressed(SC::SDL_SCANCODE_A);
        const bool right = ctx.keyboard.isKeyPressed(SC::SDL_SCANCODE_D);
        velocity.x = right ? 400.f : left ? -400.f : 0.f;
        if (grounded && ctx.keyboard.justPressed(SC::SDL_SCANCODE_SPACE, ctx.previousKeyboard)) {
            velocity.y = -600.f;
        }

        physics.step(world, ctx.dt);
        grounded = false;
        for (const engine::EntityId other : {floor, platform}) {
            grounded = resolveCollision(world, physics, player, other) || grounded;
        }

        glm::vec2& position = world.transform(player).position;
        if (position.y > 1080.f) {
            position = {spawnX, spawnY};
            velocity = {0.f, 0.f};
        }
        playerStateQueue.push({ctx.tick, position.x, position.y});
    };

    engine::SimulationThread sim(std::move(scene), gameTime, onTick);
    sim.addSubsystemThread(
        "world", [&platformSlot, worldSeconds = 0.f](const engine::SubsystemContext& ctx) mutable {
            worldSeconds += ctx.dt;
            platformSlot.publish({ctx.tick, platformX + 600.f * std::sin(worldSeconds), platformY});
        });

    std::atomic<bool> fakeNetRunning{true};
    std::thread fakeNetThread([&] {
        int messages = 0;
        std::int64_t lastTick = 0;
        auto windowStart = std::chrono::steady_clock::now();
        while (fakeNetRunning) {
            if (auto state = playerStateQueue.waitPop(100ms)) {
                ++messages;
                lastTick = state->tick;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - windowStart >= 1s) {
                engine::log::info("fake net: {} msgs/s, last tick {}", messages, lastTick);
                messages = 0;
                windowStart = now;
            }
        }
    });

    sim.start();
    std::optional<engine::RenderFrame> current;
    engine::KeyboardState previous;
    while (!window.shouldClose()) {
        window.pollEvents();
        const engine::KeyboardState keyboard = engine::KeyboardState::capture(input);
        sim.submitKeyboard(keyboard);
        if (keyboard.justPressed(SC::SDL_SCANCODE_P, previous)) {
            sim.togglePause();
        }
        if (keyboard.justPressed(SC::SDL_SCANCODE_1, previous)) {
            sim.setSpeed(0.5f);
        }
        if (keyboard.justPressed(SC::SDL_SCANCODE_2, previous)) {
            sim.setSpeed(1.f);
        }
        if (keyboard.justPressed(SC::SDL_SCANCODE_3, previous)) {
            sim.setSpeed(2.f);
        }
        previous = keyboard;

        if (auto frame = sim.takeRenderFrame()) {
            current = std::move(*frame);
        }
        if (sim.failure()) {
            engine::log::error("simulation failed; exiting");
            break;
        }

        renderer.clear({30, 60, 160, 255});
        if (current) {
            renderer.drawEntities(current->scene);
            const engine::Color barColor = current->status.paused ? engine::Color{220, 50, 50, 255}
                                                                  : engine::Color{60, 200, 90, 255};
            renderer.fillRect({20.f, 20.f}, {400.f * current->status.speed, 24.f}, barColor);
        }
        renderer.present();
    }

    sim.stop();
    fakeNetRunning = false;
    fakeNetThread.join();
    return sim.failure() ? 1 : 0;
}
