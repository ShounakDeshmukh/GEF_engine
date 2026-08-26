#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>

struct SDL_Renderer;

namespace engine {

class Window;
class World;

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
    /** Draws a filled rectangle at position (top-left corner) with the
     *  given size, in color. */
    void fillRect(glm::vec2 position, glm::vec2 size, Color color);
    /** Draws every entity that has a Shape, positioned and scaled by its
     *  Transform. */
    void drawEntities(const World& world);
    /** Presents the frame to the window. */
    void present();

private:
    struct Deleter {
        void operator()(SDL_Renderer*) const noexcept;
    };

    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
};

} // namespace engine
