#include "engine/renderer.hpp"

#include "engine/entity.hpp"
#include "engine/window.hpp"

#include <SDL3/SDL.h>
#include <stdexcept>

namespace engine {

void Renderer::Deleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

Renderer::Renderer(Window& window) : renderer_(SDL_CreateRenderer(window.nativeHandle(), nullptr)) {
    if (!renderer_) {
        throw std::runtime_error(SDL_GetError());
    }
}

Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

void Renderer::clear(Color color) {
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer_.get());
}

void Renderer::fillRect(glm::vec2 position, glm::vec2 size, Color color) {
    const SDL_FRect rect{position.x, position.y, size.x, size.y};
    SDL_SetRenderDrawColor(renderer_.get(), color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer_.get(), &rect);
}

void Renderer::drawEntities(const World& world) {
    for (const auto& [id, shape] : world.shapes()) {
        const Transform& transform = world.transform(id);
        fillRect(transform.position, shape.size * transform.scale, shape.color);
    }
}

void Renderer::present() {
    SDL_RenderPresent(renderer_.get());
}

} // namespace engine
