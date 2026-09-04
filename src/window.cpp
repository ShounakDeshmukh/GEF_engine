#include "engine/window.hpp"

#include <SDL3/SDL.h>
#include <stdexcept>
#include <utility>

namespace engine {

namespace {

std::shared_ptr<void> acquireSdlVideo() {
    static std::weak_ptr<void> weak;
    if (auto existing = weak.lock()) {
        return existing;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }
    std::shared_ptr<void> guard(nullptr, [](void*) { SDL_Quit(); });
    weak = guard;
    return guard;
}

} // namespace

void Window::Deleter::operator()(SDL_Window* window) const noexcept {
    SDL_DestroyWindow(window);
}

Window::Window(const std::string& title, int width, int height)
    : sdlVideoGuard_(acquireSdlVideo()),
      window_(SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE)), title_(title),
      width_(width), height_(height) {
    if (!window_) {
        throw std::runtime_error(SDL_GetError());
    }
}

Window::~Window() = default;
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

int Window::width() const noexcept {
    return width_;
}

int Window::height() const noexcept {
    return height_;
}

const std::string& Window::title() const noexcept {
    return title_;
}

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            shouldClose_ = true;
        } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                   event.window.windowID == SDL_GetWindowID(window_.get())) {
            shouldClose_ = true;
        } else if (event.type == SDL_EVENT_WINDOW_RESIZED &&
                   event.window.windowID == SDL_GetWindowID(window_.get())) {
            width_ = event.window.data1;
            height_ = event.window.data2;
        }
    }
}

bool Window::shouldClose() const noexcept {
    return shouldClose_;
}

SDL_Window* Window::nativeHandle() const noexcept {
    return window_.get();
}

} // namespace engine
