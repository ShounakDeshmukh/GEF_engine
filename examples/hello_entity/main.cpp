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
    const engine::TextureId orbTexture = renderer.loadTexture(assetDir + "orb.png");
    const engine::TextureId walkerTexture = renderer.loadTexture(assetDir + "grid_spritesheet.png");

    const engine::SpriteSheetId orbSheet =
        renderer.createSpriteSheet(orbTexture, engine::SpriteSheetLayout::grid({128.f, 128.f}, 4));
    const engine::SpriteSheetId walkerSheet = renderer.createSpriteSheet(
        walkerTexture, engine::SpriteSheetLayout::grid({64.f, 64.f}, 4, 2));

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

    const engine::EntityId orb = scene.createEntity();
    scene.transform(orb).position = {150.f, 750.f};
    scene.addShape(orb, {.size = {128.f, 128.f}});
    scene.addSpriteAnimation(orb, engine::SpriteAnimation::uniform(orbSheet, {0, 1, 2, 3}, 0.1f));

    const engine::EntityId walkerRight = scene.createEntity();
    scene.transform(walkerRight).position = {500.f, 800.f};
    scene.addShape(walkerRight, {.size = {64.f, 64.f}});
    scene.addSpriteAnimation(walkerRight,
                             engine::SpriteAnimation::uniform(walkerSheet, {0, 1, 2, 3}, 0.1f));

    const engine::EntityId walkerLeft = scene.createEntity();
    scene.transform(walkerLeft).position = {700.f, 800.f};
    scene.addShape(walkerLeft, {.size = {64.f, 64.f}});
    scene.addSpriteAnimation(walkerLeft,
                             engine::SpriteAnimation::uniform(walkerSheet, {4, 5, 6, 7}, 0.1f));

    engine::Clock clock;
    float totalElapsed = 0.f;

    while (!window.shouldClose()) {
        window.pollEvents();
        clock.tick();
        totalElapsed += clock.deltaSeconds();

        scene.transform(mover).position.x = 944.f + 400.f * std::sin(totalElapsed);
        engine::advanceAnimations(scene, clock.deltaSeconds());

        renderer.clear({0, 0, 255, 255});
        renderer.drawEntities(scene);
        renderer.present();
    }

    scene.destroyEntity(mover);
    engine::log::info("mover destroyed, exists = {}", scene.hasEntity(mover));

    engine::log::info("hello_entity exiting cleanly");
    return 0;
}
