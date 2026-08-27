#include "engine/renderer.hpp"

#include "engine/entity.hpp"
#include "engine/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <utility>

namespace engine {

SpriteSheetLayout SpriteSheetLayout::grid(glm::vec2 frameSize, int columns, int rows,
                                          std::optional<int> frameCount, glm::vec2 offset,
                                          glm::vec2 spacing) {
    SpriteSheetLayout layout;
    const int total = frameCount.value_or(columns * rows);
    layout.frames.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i) {
        const glm::vec2 cell{static_cast<float>(i % columns), static_cast<float>(i / columns)};
        layout.frames.push_back({offset + cell * (frameSize + spacing), frameSize});
    }
    return layout;
}

void Renderer::Deleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

void Renderer::TextureDeleter::operator()(SDL_Texture* texture) const noexcept {
    SDL_DestroyTexture(texture);
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

TextureId Renderer::loadTexture(const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_.get(), surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        throw std::runtime_error(SDL_GetError());
    }
    textures_.emplace_back(texture);
    return static_cast<TextureId>(textures_.size() - 1);
}

SpriteSheetId Renderer::createSpriteSheet(TextureId texture, SpriteSheetLayout layout) {
    static_cast<void>(textures_.at(texture));
    spriteSheets_.push_back({texture, std::move(layout.frames)});
    return static_cast<SpriteSheetId>(spriteSheets_.size() - 1);
}

void Renderer::drawTexture(TextureId texture, glm::vec2 position, glm::vec2 size, bool tiled,
                           std::optional<Rect> sourceRect) {
    SDL_Texture* handle = textures_.at(texture).get();
    const SDL_FRect rect{position.x, position.y, size.x, size.y};
    SDL_FRect srcRect{};
    const SDL_FRect* src = nullptr;
    if (sourceRect) {
        srcRect = SDL_FRect{sourceRect->origin.x, sourceRect->origin.y, sourceRect->size.x,
                            sourceRect->size.y};
        src = &srcRect;
    }
    if (tiled) {
        SDL_RenderTextureTiled(renderer_.get(), handle, src, 1.f, &rect);
    } else {
        SDL_RenderTexture(renderer_.get(), handle, src, &rect);
    }
}

void Renderer::drawEntities(const Scene& scene) {
    for (const auto& [id, shape] : scene.shapes()) {
        const Transform& transform = scene.transform(id);
        const glm::vec2 size = shape.size * transform.scale;
        if (shape.texture) {
            drawTexture(*shape.texture, transform.position, size, shape.tiled);
        } else {
            fillRect(transform.position, size, shape.color);
        }
    }
}

void Renderer::present() {
    SDL_RenderPresent(renderer_.get());
}

} // namespace engine
