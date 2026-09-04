#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <memory>
#include <optional>
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

/** A sub-region within a texture. origin is the top-left corner, in pixel
 *  coordinates. */
struct Rect {
    glm::vec2 origin{0.f, 0.f};
    glm::vec2 size{0.f, 0.f};
};

/** Handle to a sprite sheet layout created by Renderer::createSpriteSheet. */
using SpriteSheetId = std::uint32_t;

/** The set of frame rects within a sprite sheet texture. */
struct SpriteSheetLayout {
    std::vector<Rect> frames;

    /** Builds a layout from a uniform grid of frameSize cells, columns wide
     *  and rows tall, starting at offset with spacing between cells.
     *  frameCount truncates the result to fewer than columns * rows frames,
     *  for a sheet whose last row isn't fully filled. */
    static SpriteSheetLayout grid(glm::vec2 frameSize, int columns, int rows = 1,
                                  std::optional<int> frameCount = std::nullopt,
                                  glm::vec2 offset = {0.f, 0.f}, glm::vec2 spacing = {0.f, 0.f});
};

/** Determines how renderer coordinates are presented in the window.
 *  Constant uses native output pixels. Proportional uniformly scales a
 *  1920x1080 logical frame to fit while preserving its aspect ratio. */
enum class ScalingMode { Constant, Proportional };

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
    /** Registers a sprite sheet layout for texture and returns its id.
     *  Throws std::out_of_range if texture is not a valid id. */
    SpriteSheetId createSpriteSheet(TextureId texture, SpriteSheetLayout layout);
    /** Draws a texture at position (top-left corner). Stretched to fill
     *  size, or tiled across it if tiled is true. sourceRect, if set,
     *  draws only that sub-region of the texture instead of the whole
     *  thing. Throws std::out_of_range if texture is not a valid id. */
    void drawTexture(TextureId texture, glm::vec2 position, glm::vec2 size, bool tiled = false,
                     std::optional<Rect> sourceRect = std::nullopt);
    /** Draws every entity that has a Shape, positioned and scaled by its
     *  Transform. Uses the shape's texture if it has one, otherwise its
     *  color. */
    void drawEntities(const Scene& scene);
    /** Presents the frame to the window. */
    void present();

    /** Returns the current rendering scaling mode. */
    ScalingMode scalingMode() const noexcept;

    /** Selects the rendering scaling mode. Throws std::runtime_error if SDL
     *  cannot change the logical presentation. */
    void setScalingMode(ScalingMode mode);

    /** Switches between constant and proportional scaling. Throws
     *  std::runtime_error if SDL cannot change the logical presentation. */
    void toggleScalingMode();

private:
    struct Deleter {
        void operator()(SDL_Renderer*) const noexcept;
    };
    struct TextureDeleter {
        void operator()(SDL_Texture*) const noexcept;
    };
    struct SpriteSheetData {
        TextureId texture;
        std::vector<Rect> frames;
    };

    static constexpr int referenceWidth_ = 1920;
    static constexpr int referenceHeight_ = 1080;

    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
    std::vector<std::unique_ptr<SDL_Texture, TextureDeleter>> textures_;
    std::vector<SpriteSheetData> spriteSheets_;

    ScalingMode scalingMode_ = ScalingMode::Constant;
};

} // namespace engine
