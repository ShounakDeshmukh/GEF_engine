#include <chrono>
#include <cmath>
#include <engine/engine.hpp>

int main() {
    engine::log::init();
    engine::log::info("starting hello_entity");

    engine::Window window("hello_entity", 1920, 1080);
    engine::Renderer renderer(window);

    engine::World world;

    const engine::EntityId mover = world.create();
    world.transform(mover).position = {944.f, 524.f};
    world.addShape(mover, {.size = {32.f, 32.f}, .color = {255, 0, 0, 255}});

    const engine::EntityId ground = world.create();
    world.transform(ground).position = {0.f, 1000.f};
    world.addShape(ground, {.size = {1920.f, 80.f}, .color = {0, 255, 0, 255}});

    const auto start = std::chrono::steady_clock::now();

    while (!window.shouldClose()) {
        window.pollEvents();

        const float elapsedSeconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        world.transform(mover).position.x = 944.f + 400.f * std::sin(elapsedSeconds);

        renderer.clear({0, 0, 255, 255});
        renderer.drawEntities(world);
        renderer.present();
    }

    world.destroy(mover);
    engine::log::info("mover destroyed, exists = {}", world.exists(mover));

    engine::log::info("hello_entity exiting cleanly");
    return 0;
}
