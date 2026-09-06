#include <cmath>
#include <engine/engine.hpp>
#include <string>

int main() {
    engine::log::init();
    engine::log::info("starting collision_demo");

    engine::Window window("collision_demo", 1920, 1080);
    engine::Renderer renderer(window);
    engine::PhysicsSystem physics(980.f);

    engine::Scene scene;

    const engine::EntityId mover = scene.createEntity();
    scene.transform(mover).position = {944.f, 524.f};
    scene.addShape(mover, {.size = {32.f, 32.f}, .color = {255, 0, 0, 255}});
    scene.addCollider(mover, {.size = {32.f, 32.f}});

    const engine::EntityId mover2 = scene.createEntity();
    scene.transform(mover2).position = {900.f, 524.f};
    scene.addShape(mover2, {.size = {32.f, 32.f}, .color = {0, 255, 0, 255}});
    scene.addCollider(mover2, {.size = {32.f, 32.f}});

    const engine::EntityId collisionDetector = scene.createEntity();
    scene.transform(collisionDetector).position = {944.f, 650.f};
    scene.addShape(collisionDetector, {.size = {32.f, 32.f}, .color = {255, 255, 0, 255}});
    scene.addCollider(collisionDetector, {.size = {32.f, 32.f}});

    engine::Timeline realTime;
    engine::Timeline gameTime(realTime, 60);
    engine::Stepper sim(gameTime);

    while (!window.shouldClose()) {
        window.pollEvents();

        sim.beginFrame();
        while (sim.step()) {
            const float stepSeconds = gameTime.tickSeconds();
            const float elapsed = static_cast<float>(sim.tickIndex()) * stepSeconds;

            scene.transform(mover).position.x = 944.f + 400.f * std::sin(elapsed);
            scene.transform(mover2).position.x = 900.f + 300.f * std::sin(elapsed);
            engine::advanceAnimations(scene, stepSeconds);

            if (physics.isCollision(scene, mover, mover2)) {
                scene.getShape(collisionDetector)->color = {255, 0, 0, 255};
            } else {
                scene.getShape(collisionDetector)->color = {0, 0, 255, 255};
            }
        }

        renderer.clear({0, 0, 255, 255});
        renderer.drawEntities(scene);
        renderer.present();
    }

    scene.destroyEntity(mover);
    scene.destroyEntity(mover2);
    scene.destroyEntity(collisionDetector);
    engine::log::info("mover destroyed, exists = {}", scene.hasEntity(mover));

    engine::log::info("collision demo exiting cleanly");
    return 0;
}
