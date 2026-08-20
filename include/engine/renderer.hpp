#pragma once

#include <cstdint>
#include <memory>

struct SDL_Renderer;

namespace engine {

class Window;

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    void clear(Color color);
    void present();

private:
    struct Deleter {
        void operator()(SDL_Renderer*) const noexcept;
    };

    std::unique_ptr<SDL_Renderer, Deleter> renderer_;
};

} // namespace engine
