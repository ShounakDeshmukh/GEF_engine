#include <chrono>
#include <cmath>
#include <engine/engine.hpp>
#include <string>

int main() {
    engine::log::init();
    engine::log::info("starting hello_entity");

    engine::Window window("hello_entity", 1920, 1080);
    engine::Renderer renderer(window);

    const std::string assetDir = HELLO_ENTITY_ASSET_DIR;
    const engine::TextureId circleTexture = renderer.loadTexture(assetDir + "texture.png");
    const engine::TextureId checkerTexture = renderer.loadTexture(assetDir + "checker.png");

    engine::Scene scene;

    const engine::EntityId mover = scene.createEntity();
    scene.transform(mover).position = {944.f, 524.f};
    scene.addShape(mover, {.size = {32.f, 32.f}, .color = {255, 0, 0, 255}});

    const engine::EntityId ground = scene.createEntity();
    scene.transform(ground).position = {0.f, 1000.f};
    scene.addShape(ground, {.size = {1920.f, 80.f}, .texture = checkerTexture, .tiled = true});

    const engine::EntityId textured = scene.createEntity();
    scene.transform(textured).position = {944.f, 300.f};
    scene.addShape(textured, {.size = {64.f, 64.f}, .texture = circleTexture});

    const auto start = std::chrono::steady_clock::now();

    while (!window.shouldClose()) {
        window.pollEvents();

        const float elapsedSeconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        scene.transform(mover).position.x = 944.f + 400.f * std::sin(elapsedSeconds);

        renderer.clear({0, 0, 255, 255});
        renderer.drawEntities(scene);
        renderer.present();
    }

    scene.destroyEntity(mover);
    engine::log::info("mover destroyed, exists = {}", scene.hasEntity(mover));

    engine::log::info("hello_entity exiting cleanly");
    return 0;
}
