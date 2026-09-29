// multithread_demo: a small platformer whose update work is split across threads with
// engine::SimulationThread.
//
// Threads:
//   main           SDL events, keyboard capture, pause/speed requests, rendering.
//   sim            owns the Scene; runs onTick once per fixed tick: player input, physics,
//                  collision.
//   world          a subsystem thread that computes the moving platform's position. It never
//                  touches the Scene; it publishes positions the sim applies.
//   fakeNetThread  stands in for networking: drains the player state onTick pushes every tick
//                  and logs how many messages arrive per second.
//
// Controls:
//   A / D      move left / right
//   Space      jump
//   P          toggle pause
//   1 / 2 / 3  speed 0.5 / 1.0 / 2.0
// The top-left bar's width shows speed; it turns red while paused. The fake net log shows about
// 60 msgs/s at speed 1.0, 30 at 0.5, 120 at 2.0 and 0 while paused, because the sim pushes one
// message per tick and ticks follow the Timeline.

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

// Sent from the sim to the fake net thread once per tick. Every message carries the tick it was
// produced on, so a receiver can order it or match it against its own ticks.
struct PlayerState {
    std::int64_t tick;
    float x, y;
};

// Sent from the world subsystem to the sim. The subsystem steps the Timeline on its own, so it
// can be up to one frame ahead of or behind the sim; the tick says which tick produced it.
struct PlatformState {
    std::int64_t tick;
    float x, y;
};

constexpr float spawnX = 200.f;
constexpr float spawnY = 800.f;
constexpr float platformX = 832.f;
constexpr float platformY = 700.f;

// Creates a solid, visible, collidable rectangle.
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

    // A thin vertical overlap means the player hit a top or bottom face; otherwise a side.
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
    // ---------------------------------------------------------------------------------------
    // Setup, on main. Everything here happens before any other thread exists.
    // ---------------------------------------------------------------------------------------
    engine::log::init();

    engine::Window window("multithread_demo", 1920, 1080);
    engine::Renderer renderer(window);
    engine::InputHandler input;
    engine::PhysicsSystem physics(980.f);

    // realTime follows the wall clock; gameTime runs 60 ticks per simulated second on top of it.
    // Pause and speed are applied to gameTime, and both the sim and the world subsystem step it.
    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);

    // Build the whole Scene now. Once it is handed to the SimulationThread below, only the sim
    // thread may touch it; main only ever sees copies.
    engine::Scene scene;
    const engine::EntityId floor =
        addBlock(scene, {0.f, 1000.f}, {1920.f, 80.f}, {60, 170, 80, 255});
    const engine::EntityId platform =
        addBlock(scene, {platformX, platformY}, {256.f, 32.f}, {230, 140, 40, 255});
    const engine::EntityId player =
        addBlock(scene, {spawnX, spawnY}, {64.f, 64.f}, {240, 240, 255, 255});
    scene.addRigidBody(player);

    // Channels between threads. Everything that crosses a thread boundary goes through one of
    // these, by value:
    //   platformSlot      world -> sim. LatestValue: only the newest platform position matters,
    //                     so a newer one overwrites an unread older one.
    //   playerStateQueue  sim -> fakeNetThread. ThreadSafeQueue: every tick's state matters, in
    //                     order. Capacity 256 drops the oldest if the reader falls behind.
    engine::threading::LatestValue<PlatformState> platformSlot;
    engine::threading::ThreadSafeQueue<PlayerState> playerStateQueue(256);

    // State captured by onTick. From sim.start() until sim.stop() it belongs to the sim thread,
    // so main must not read or write it in between.
    PlatformState lastPlatform{0, platformX, platformY};
    bool grounded = false;

    // ---------------------------------------------------------------------------------------
    // onTick: runs on the sim thread, exactly once per game tick. At speed 2.0 it runs 120 times
    // per real second, at 0.5 it runs 30 times, and while paused it does not run at all.
    // ctx.dt is the fixed simulated length of one tick (1/60 s), whatever the speed.
    // ---------------------------------------------------------------------------------------
    auto onTick = [&](const engine::TickContext& ctx) {
        engine::Scene& world = ctx.scene;

        // Apply the newest platform position from the world subsystem. If it published nothing
        // new since the last tick, keep the previous one.
        if (auto latest = platformSlot.take()) {
            lastPlatform = *latest;
        }
        world.transform(platform).position = {lastPlatform.x, lastPlatform.y};

        // Keyboard state was captured on main and handed over with submitKeyboard(). Nothing
        // here calls SDL. previousKeyboard is what the previous tick saw, so justPressed is true
        // on exactly one tick per key press, even when one frame runs several ticks.
        glm::vec2& velocity = world.getRigidBody(player)->velocity;
        const bool left = ctx.keyboard.isKeyPressed(SC::SDL_SCANCODE_A);
        const bool right = ctx.keyboard.isKeyPressed(SC::SDL_SCANCODE_D);
        velocity.x = right ? 400.f : left ? -400.f : 0.f;
        if (grounded && ctx.keyboard.justPressed(SC::SDL_SCANCODE_SPACE, ctx.previousKeyboard)) {
            velocity.y = -600.f;
        }

        // Physics and collision advance by the fixed dt, so behaviour is identical at any speed;
        // speed only changes how many ticks run per real second.
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

        // The networking seam: one message per tick, stamped with the tick. Because this runs
        // once per tick, the message rate follows game speed and stops while paused.
        playerStateQueue.push({ctx.tick, position.x, position.y});
    };

    // The SimulationThread takes ownership of the Scene. Nothing runs until start().
    engine::SimulationThread sim(std::move(scene), gameTime, onTick);

    // ---------------------------------------------------------------------------------------
    // The world subsystem: a second fixed-step thread on the same gameTime. It gets only
    // {tick, dt}, never the Scene, and hands its result to the sim through platformSlot.
    //
    // Nuances:
    //  - It steps gameTime independently, so pause and speed apply to it automatically.
    //  - It must never block waiting on the sim thread. A pause or other posted task waits for
    //    the subsystem callback in progress, so a callback waiting on the sim would deadlock.
    //  - Pausing discards subsystem ticks that were due but not yet run, so once main sees
    //    "paused" the platform does not move again until unpause.
    //  - worldSeconds lives in the lambda, so only this thread touches it. It accumulates dt
    //    per tick instead of computing tick * dt, which stays correct across tick-rate changes.
    // Subsystems must be added before start().
    // ---------------------------------------------------------------------------------------
    sim.addSubsystemThread(
        "world", [&platformSlot, worldSeconds = 0.f](const engine::SubsystemContext& ctx) mutable {
            worldSeconds += ctx.dt;
            platformSlot.publish({ctx.tick, platformX + 600.f * std::sin(worldSeconds), platformY});
        });

    // ---------------------------------------------------------------------------------------
    // fakeNetThread: a plain std::thread standing in for the networking code. It never touches
    // the Scene; it only drains playerStateQueue. waitPop times out after 100 ms so the loop
    // can notice fakeNetRunning turning false even when no messages arrive (e.g. while paused).
    // ---------------------------------------------------------------------------------------
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

    // Spawns the sim thread and one thread per subsystem.
    sim.start();

    // ---------------------------------------------------------------------------------------
    // Main loop, on main. Each iteration:
    //   1. pump SDL events (this is when SDL updates its keyboard array),
    //   2. copy the keyboard into a KeyboardState and hand it to the sim,
    //   3. turn key presses into pause/speed requests; these are queued and applied by the sim
    //      at the start of its next frame, never directly by main,
    //   4. take the newest RenderFrame the sim published, if any,
    //   5. draw it.
    // Main never waits on the sim: if no new frame is ready it redraws the last one.
    // ---------------------------------------------------------------------------------------
    std::optional<engine::RenderFrame> current;
    engine::KeyboardState previous;
    while (!window.shouldClose()) {
        window.pollEvents();
        const engine::KeyboardState keyboard = engine::KeyboardState::capture(input);
        sim.submitKeyboard(keyboard);

        // Edge-detected against the previous main-loop iteration, so holding a key sends one
        // request, not one per frame.
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

        // A RenderFrame is a full copy of the Scene plus status, so drawing it never races the
        // sim, which keeps updating its own Scene meanwhile.
        if (auto frame = sim.takeRenderFrame()) {
            current = std::move(*frame);
        }

        // An exception on the sim or a subsystem thread stops all of them; main just exits.
        if (sim.failure()) {
            engine::log::error("simulation failed; exiting");
            break;
        }

        renderer.clear({30, 60, 160, 255});
        if (current) {
            renderer.drawEntities(current->scene);
            // Status bar: width follows speed, red while paused. Read from the frame's status,
            // not from gameTime, which belongs to the sim thread while it runs.
            const engine::Color barColor = current->status.paused ? engine::Color{220, 50, 50, 255}
                                                                  : engine::Color{60, 200, 90, 255};
            renderer.fillRect({20.f, 20.f}, {400.f * current->status.speed, 24.f}, barColor);
        }
        renderer.present();
    }

    // ---------------------------------------------------------------------------------------
    // Shutdown. stop() joins the sim and subsystem threads, so after it returns gameTime and
    // the state onTick captured belong to main again. The fake net thread is stopped last
    // because it only reads from the queue.
    // ---------------------------------------------------------------------------------------
    sim.stop();
    fakeNetRunning = false;
    fakeNetThread.join();
    return sim.failure() ? 1 : 0;
}
