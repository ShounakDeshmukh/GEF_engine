#include <engine/engine.hpp>

int main() {
    engine::log::init();
    engine::log::info("starting scaling_demo; press P to toggle scaling mode");

    engine::Window window("scaling_demo", 1280, 720);
    engine::Renderer renderer(window);
    engine::InputHandler input;

    engine::Scene scene;
    const engine::EntityId entity = scene.createEntity();
    scene.transform(entity).position = {910.f, 490.f};
    scene.addShape(entity, {.size = {100.f, 100.f}, .color = {255, 0, 0, 255}});

    bool scalingKeyWasPressed = false;

    while (!window.shouldClose()) {
        window.pollEvents();

        const bool scalingKeyIsPressed = input.isKeyPressed(engine::SC::SDL_SCANCODE_P);
        if (scalingKeyIsPressed && !scalingKeyWasPressed) {
            renderer.toggleScalingMode();
        }
        scalingKeyWasPressed = scalingKeyIsPressed;

        renderer.clear({0, 0, 0, 255});
        renderer.fillRect({0.f, 0.f}, {1920.f, 1080.f}, {0, 0, 255, 255});
        renderer.drawEntities(scene);
        renderer.present();
    }

    engine::log::info("scaling_demo exiting cleanly");
    return 0;
}
