#include "engine/renderer.hpp"

#include "engine/entity.hpp"
#include "engine/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
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

void Renderer::FontDeleter::operator()(TTF_Font* font) const noexcept {
    TTF_CloseFont(font);
}


Renderer::Renderer(Window& window)
    : renderer_(SDL_CreateRenderer(window.nativeHandle(), nullptr)) {
    if (!renderer_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!TTF_Init()) {
        throw std::runtime_error(SDL_GetError());
    }
}

Renderer::~Renderer() {
    textCache_.clear();
    textures_.clear();
    fonts_.clear();
    TTF_Quit();
}
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

FontId Renderer::loadFont(const std::string& path, float size) {
    TTF_Font* font = TTF_OpenFont(path.c_str(), size);

    if (!font) {
        throw std::runtime_error(SDL_GetError());
    }

    fonts_.emplace_back(font);
    return static_cast<FontId>(fonts_.size() - 1);
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
    //Render shapes/sprites/spriteSheets
    for (const auto& [id, shape] : scene.shapes()) {
        const Transform& transform = scene.transform(id);
        const glm::vec2 size = shape.size * transform.scale;
        if (const SpriteAnimation* anim = scene.getSpriteAnimation(id)) {
            const SpriteSheetData& sheet = spriteSheets_.at(anim->sheet);
            const std::uint32_t frameIndex = anim->frames.at(anim->currentFrame).index;
            drawTexture(sheet.texture, transform.position, size, false,
                        sheet.frames.at(frameIndex));
        } else if (shape.texture) {
            drawTexture(*shape.texture, transform.position, size, shape.tiled);
        } else {
            fillRect(transform.position, size, shape.color);
        }
    }

    //Render Text
    for (const auto& [id, text] : scene.texts()) {
        const Transform& transform = scene.transform(id);

        drawText(id, text, transform.position);
    }

}

void Renderer::drawText(EntityId id, const Text& text, glm::vec2 position) {

    auto it = textCache_.find(id);

    const bool isUpdateNeeded = it == textCache_.end() || //doesn't exist
                                it->second.font != text.font ||
                                it->second.val != text.val ||
                                it->second.color.r != text.color.r ||
                                it->second.color.g != text.color.g ||
                                it->second.color.b != text.color.b ||
                                it->second.color.a != text.color.a;

    if (isUpdateNeeded) {
        rebuildTextCache(id, text);
        it = textCache_.find(id);
    }

    const TextCacheData& data = it->second;


    const SDL_FRect dest{position.x, position.y, data.size.x, data.size.y};

    SDL_RenderTexture(renderer_.get(), data.texture.get(), nullptr, &dest);

}


void Renderer::rebuildTextCache(EntityId id, const Text& text) {
    TTF_Font* fontHandle = fonts_.at(text.font).get();
    SDL_Color sdlColor {text.color.r, text.color.g, text.color.b, text.color.a};

    SDL_Surface* surface = TTF_RenderText_Blended(fontHandle, text.val.c_str(), text.val.size(), sdlColor);

    if(!surface) {throw std::runtime_error(SDL_GetError());}

    //fine that its a raw pointer, only unique ptr points to it after function ends
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_.get(), surface);

    if(!texture) {SDL_DestroySurface(surface); throw std::runtime_error(SDL_GetError());}

    TextCacheData textData{.font = text.font, .val = text.val, .color = text.color, 
        .texture = std::unique_ptr<SDL_Texture, TextureDeleter>(texture), 
        .size = {static_cast<float>(surface->w), static_cast<float>(surface->h)}};

    SDL_DestroySurface(surface);

    textCache_.insert_or_assign(id, std::move(textData));
}


void Renderer::present() {
    SDL_RenderPresent(renderer_.get());
}

ScalingMode Renderer::scalingMode() const noexcept {
    return scalingMode_;
}

void Renderer::setScalingMode(ScalingMode mode) {
    const SDL_RendererLogicalPresentation presentation =
        mode == ScalingMode::Proportional ? SDL_LOGICAL_PRESENTATION_LETTERBOX
                                          : SDL_LOGICAL_PRESENTATION_DISABLED;
    const int logicalWidth = mode == ScalingMode::Proportional ? referenceWidth_ : 0;
    const int logicalHeight = mode == ScalingMode::Proportional ? referenceHeight_ : 0;

    if (!SDL_SetRenderLogicalPresentation(renderer_.get(), logicalWidth, logicalHeight,
                                          presentation)) {
        throw std::runtime_error(SDL_GetError());
    }

    scalingMode_ = mode;
}

void Renderer::toggleScalingMode() {
    if (scalingMode_ == ScalingMode::Constant) {
        setScalingMode(ScalingMode::Proportional);
    } else {
        setScalingMode(ScalingMode::Constant);
    }
}

} // namespace engine
