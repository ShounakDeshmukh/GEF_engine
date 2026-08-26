#pragma once

#include <cstdint>
#include <memory>

struct SDL_Renderer;

namespace engine {

class Window;

/** RGBA color, 0-255 per channel. */
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/** Draws into a Window using SDL's hardware-accelerated renderer. */
class Renderer {
public:
    /** Creates a renderer bound to the given window. */
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    /** Fills the entire frame with color. */
    void clear(Color color);
    /** Presents the frame to the window. */
    void present();

private:
    struct Deleter {
        void operator()(SDL_Renderer*) const noexcept;
    };

    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
};

} // namespace engine
