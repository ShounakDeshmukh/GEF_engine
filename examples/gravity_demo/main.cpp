#include <engine/engine.hpp>
#include <string>

int main() {
    constexpr int windowWidth = 1920;
    constexpr int windowHeight = 1080;

    constexpr float orbSize = 128.f;
    constexpr float topStart = -orbSize;
    constexpr float orbX = (static_cast<float>(windowWidth) - orbSize) * 0.5f;

    engine::log::init();
    engine::log::info("starting gravity_demo");

    engine::Window window("gravity_demo", windowWidth, windowHeight);
    engine::Renderer renderer(window);

    const std::string assetDir = GRAVITY_DEMO_ASSET_DIR;
    const engine::TextureId orbTexture = renderer.loadTexture(assetDir + "orb.png");
    const engine::SpriteSheetId orbSheet =
        renderer.createSpriteSheet(orbTexture, engine::SpriteSheetLayout::grid({128.f, 128.f}, 4));

    engine::Scene scene;
    const engine::EntityId orb = scene.createEntity();
    scene.transform(orb).position = {orbX, topStart};
    scene.addShape(orb, {.size = {orbSize, orbSize}});
    scene.addSpriteAnimation(orb,
                             engine::SpriteAnimation::uniform(orbSheet, {0, 1, 2, 3}, 0.1f));
    scene.addRigidBody(orb);

    engine::PhysicsSystem physics(980.f);
    engine::Clock clock;

    while (!window.shouldClose()) {
        window.pollEvents();
        clock.tick();

        const float deltaSeconds = clock.deltaSeconds();

        engine::advanceAnimations(scene, deltaSeconds);

        physics.step(scene, deltaSeconds);

        engine::Transform& transform = scene.transform(orb);
        if (transform.position.y > static_cast<float>(windowHeight)) {
            transform.position = {orbX, topStart};
            scene.getRigidBody(orb)->velocity = {};
        }

        renderer.clear({0, 0, 0, 255});
        renderer.drawEntities(scene);
        renderer.present();
    }

    engine::log::info("gravity_demo exiting cleanly");

    return 0;
}
