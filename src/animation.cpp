#include "engine/animation.hpp"

#include "engine/entity.hpp"

namespace engine {

SpriteAnimation SpriteAnimation::uniform(SpriteSheetId sheet, std::vector<std::uint32_t> indices,
                                         float duration, bool loop) {
    SpriteAnimation anim;
    anim.sheet = sheet;
    anim.loop = loop;
    anim.frames.reserve(indices.size());
    for (std::uint32_t index : indices) {
        anim.frames.push_back({index, duration});
    }
    return anim;
}

void advanceAnimations(Scene& scene, float deltaSeconds) {
    for (auto& entry : scene.spriteAnimations()) {
        SpriteAnimation& anim = entry.second;
        if (anim.frames.empty()) {
            continue;
        }
        anim.elapsed += deltaSeconds;
        while (anim.elapsed >= anim.frames[anim.currentFrame].duration) {
            anim.elapsed -= anim.frames[anim.currentFrame].duration;
            if (anim.currentFrame + 1 < anim.frames.size()) {
                ++anim.currentFrame;
            } else if (anim.loop) {
                anim.currentFrame = 0;
            } else {
                anim.elapsed = 0.f;
                break;
            }
        }
    }
}

} // namespace engine
