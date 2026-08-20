#pragma once

#include <memory>
#include <string>

struct SDL_Window;

namespace engine {

class Renderer;

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    int width() const noexcept;
    int height() const noexcept;
    const std::string& title() const noexcept;

    // Pumps the OS event queue once. Call before checking shouldClose().
    void pollEvents();

    // True once the user has requested this window be closed.
    bool shouldClose() const noexcept;

private:
    friend class Renderer;
    SDL_Window* nativeHandle() const noexcept;

    struct Deleter {
        void operator()(SDL_Window*) const noexcept;
    };

    std::shared_ptr<void> sdlVideoGuard_;
    std::unique_ptr<SDL_Window, Deleter> window_;
    std::string title_;
    int width_ = 0;
    int height_ = 0;
    bool shouldClose_ = false;
};

} // namespace engine
