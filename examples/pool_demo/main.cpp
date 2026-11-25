// The custom memory allocator, used two ways:
//
//  1. Pooled Scene storage. The Scene reserves memory up front for a fixed number of entities
//     and for each pooled component type (Scene::reserveEntities / Scene::reservePool<T>).
//     Hold Space to fire. Only kMaxBullets bullets fit in the pool, so firing stops when it is
//     full and starts again as bullets leave the screen and give their slots back. The bars at
//     the top show how full each pool is.
//
//  2. A game-side type on memory::objectAllocator. Sparks are not Scene entities; the game
//     creates them directly. The same code runs on a pool (default) or on the heap
//     (`pool_demo --heap`), the only difference being how the allocator is constructed.
//
// A / D move the turret.
#include <engine/engine.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {

// Pool sizes the game designer picks: object type and how many of it.
constexpr std::size_t kMaxBullets = 16;
constexpr std::size_t kMaxEntities = kMaxBullets + 1; // bullets + the turret
constexpr std::size_t kMaxSparks = 96;

constexpr float kWidth = 1280.f;
constexpr float kHeight = 720.f;
constexpr float kTurretSpeed = 600.f;
constexpr float kBulletSpeed = 700.f;
constexpr float kFireInterval = 0.05f; // seconds between shots while Space is held
constexpr glm::vec2 kBulletSize{8.f, 20.f};

struct Bullet {};

/** A short-lived particle, owned by the game rather than the Scene. */
struct Spark {
    glm::vec2 position;
    glm::vec2 velocity;
    float life = 0.4f; // seconds left
};

constexpr engine::Color kBackground{20, 20, 35, 255};
constexpr engine::Color kTurretColor{90, 200, 120, 255};
constexpr engine::Color kBulletColor{250, 220, 90, 255};
constexpr engine::Color kSparkColor{255, 140, 60, 255};
constexpr engine::Color kBarBack{60, 60, 80, 255};
constexpr engine::Color kBarFill{120, 170, 255, 255};
constexpr engine::Color kBarFull{230, 80, 70, 255};

/** A usage bar for an allocator: size out of capacity. */
void drawPoolBar(engine::Renderer& renderer, glm::vec2 position, std::size_t size,
                 std::size_t capacity) {
    constexpr glm::vec2 kBar{240.f, 12.f};
    renderer.fillRect(position, kBar, kBarBack);
    const float fraction = static_cast<float>(size) / static_cast<float>(capacity);
    renderer.fillRect(position, {kBar.x * fraction, kBar.y}, size == capacity ? kBarFull : kBarFill);
}

} // namespace

int main(int argc, char** argv) {
    engine::log::init();
    const bool heapSparks = argc > 1 && std::string_view(argv[1]) == "--heap";

    engine::Window window("pool_demo", static_cast<int>(kWidth), static_cast<int>(kHeight));
    engine::Renderer renderer(window);
    engine::InputHandler input;
    engine::PhysicsSystem physics(0.f);
    engine::Scene scene;

    // 1. Reserve the Scene's pools before creating anything. Every Transform, Bullet,
    //    RigidBody and Shape now lives in memory allocated here, once.
    scene.reserveEntities(kMaxEntities);
    scene.reservePool<Bullet>(kMaxBullets);
    scene.reservePool<engine::RigidBody>(kMaxBullets);
    scene.reservePool<engine::Shape>(kMaxEntities);

    // 2. A game-side allocator: pool mode with room for kMaxSparks, or heap mode.
    engine::memory::objectAllocator<Spark> sparkAllocator =
        heapSparks ? engine::memory::objectAllocator<Spark>()
                   : engine::memory::objectAllocator<Spark>(kMaxSparks);
    std::vector<Spark*> sparks;

    engine::log::info("pool_demo: {} entities, {} bullets pooled; sparks on the {}",
                      scene.transforms().capacity(), scene.all<Bullet>().capacity(),
                      sparkAllocator.pooled() ? "pool" : "heap");

    const engine::EntityId turret = scene.createEntity();
    scene.transform(turret).position = {kWidth / 2.f - 32.f, kHeight - 60.f};
    scene.addShape(turret, {.size = {64.f, 32.f}, .color = kTurretColor});

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper sim(gameTime);

    float fireCooldown = 0.f;
    bool wasFull = false;
    std::vector<engine::EntityId> spent;

    while (!window.shouldClose()) {
        window.pollEvents();

        sim.beginFrame();
        while (sim.step()) {
            const float dt = gameTime.tickSeconds();

            glm::vec2& turretPos = scene.transform(turret).position;
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_A)) {
                turretPos.x -= kTurretSpeed * dt;
            }
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_D)) {
                turretPos.x += kTurretSpeed * dt;
            }
            turretPos.x = std::clamp(turretPos.x, 0.f, kWidth - 64.f);

            // Fire. The pool's full() says whether another add<Bullet> would throw
            // std::length_error, so the game can hold fire instead.
            fireCooldown -= dt;
            const bool full = scene.all<Bullet>().full();
            if (full != wasFull) {
                if (full) {
                    engine::log::info("bullet pool full ({} live), holding fire",
                                      scene.all<Bullet>().size());
                } else {
                    engine::log::info("bullet pool has room again");
                }
                wasFull = full;
            }
            if (input.isKeyPressed(engine::SC::SDL_SCANCODE_SPACE) && fireCooldown <= 0.f &&
                !full) {
                const engine::EntityId bullet = scene.createEntity();
                scene.transform(bullet).position = {turretPos.x + 32.f - kBulletSize.x / 2.f,
                                                    turretPos.y - kBulletSize.y};
                scene.add<Bullet>(bullet);
                scene.addRigidBody(bullet, {{0.f, -kBulletSpeed}});
                scene.addShape(bullet, {.size = kBulletSize, .color = kBulletColor});
                fireCooldown = kFireInterval;
            }

            physics.step(scene, dt);

            // Bullets past the top edge burst into sparks and are destroyed, which returns
            // their Transform, Bullet, RigidBody and Shape slots to the pools.
            spent.clear();
            for (const auto& [id, tag] : scene.all<Bullet>()) {
                if (scene.transform(id).position.y + kBulletSize.y < 0.f) {
                    spent.push_back(id);
                }
            }
            for (const engine::EntityId id : spent) {
                const float x = scene.transform(id).position.x;
                for (int i = 0; i < 6 && !sparkAllocator.full(); ++i) {
                    const float vx = (static_cast<float>(i) - 2.5f) * 80.f;
                    sparks.push_back(sparkAllocator.create(Spark{{x, 4.f}, {vx, 220.f}}));
                }
                scene.destroyEntity(id);
            }

            // Age the sparks; dead ones go back to their allocator.
            for (Spark*& spark : sparks) {
                spark->position += spark->velocity * dt;
                spark->life -= dt;
                if (spark->life <= 0.f) {
                    sparkAllocator.destroy(spark);
                    spark = nullptr;
                }
            }
            std::erase(sparks, nullptr);
        }

        renderer.clear(kBackground);
        renderer.drawEntities(scene);
        for (const Spark* spark : sparks) {
            renderer.fillRect(spark->position, {6.f, 6.f}, kSparkColor);
        }

        // HUD: entity pool, bullet pool, and the spark pool (heap mode has no bar).
        drawPoolBar(renderer, {16.f, 16.f}, scene.transforms().size(),
                    scene.transforms().capacity());
        drawPoolBar(renderer, {16.f, 36.f}, scene.all<Bullet>().size(),
                    scene.all<Bullet>().capacity());
        if (sparkAllocator.pooled()) {
            drawPoolBar(renderer, {16.f, 56.f}, sparkAllocator.size(), sparkAllocator.capacity());
        }
        renderer.present();
    }

    // Every object must go back to its allocator before the allocator is destroyed.
    for (Spark* spark : sparks) {
        sparkAllocator.destroy(spark);
    }
    return 0;
}
