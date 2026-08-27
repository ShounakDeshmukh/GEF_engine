#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace engine {

class Window;
class Scene;

/** RGBA color, 0-255 per channel. */
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/** Handle to a texture loaded by Renderer::loadTexture. */
using TextureId = std::uint32_t;

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
    /** Loads an image file into a GPU texture and returns its id. Throws
     *  std::runtime_error if the file can't be loaded. */
    TextureId loadTexture(const std::string& path);
    /** Draws a texture at position (top-left corner). Stretched to fill
     *  size, or tiled across it if tiled is true. Throws std::out_of_range
     *  if texture is not a valid id. */
    void drawTexture(TextureId texture, glm::vec2 position, glm::vec2 size, bool tiled = false);
    /** Draws every entity that has a Shape, positioned and scaled by its
     *  Transform. Uses the shape's texture if it has one, otherwise its
     *  color. */
    void drawEntities(const Scene& scene);
    /** Presents the frame to the window. */
    void present();

private:
    struct Deleter {
        void operator()(SDL_Renderer*) const noexcept;
    };
    struct TextureDeleter {
        void operator()(SDL_Texture*) const noexcept;
    };

    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
    std::vector<std::unique_ptr<SDL_Texture, TextureDeleter>> textures_;
};

} // namespace engine
