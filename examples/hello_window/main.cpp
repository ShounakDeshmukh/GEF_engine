#include <engine/engine.hpp>

int main() {
    engine::log::init();
    engine::log::info("starting hello_window ({}x{})", 1920, 1080);

    engine::Window window("hello_window", 1920, 1080);
    engine::Renderer renderer(window);

    while (!window.shouldClose()) {
        window.pollEvents();
        renderer.clear({0, 0, 255, 255});
        renderer.present();
    }

    engine::log::info("hello_window exiting cleanly");
    return 0;
}
