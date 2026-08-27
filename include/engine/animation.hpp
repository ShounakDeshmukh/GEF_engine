#pragma once

#include "engine/renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine {

class Scene;

/** One frame within a SpriteSheetLayout, and how long it is shown. */
struct AnimationFrame {
    std::uint32_t index;
    float duration = 0.1f;
};

/** A sprite sheet animation and its live playback state. */
struct SpriteAnimation {
    SpriteSheetId sheet;
    std::vector<AnimationFrame> frames; // in play order; indices need not be contiguous
    bool loop = true;
    std::size_t currentFrame = 0;
    float elapsed = 0.f; // time accumulated toward the next frame change

    /** Builds a SpriteAnimation for sheet where every frame in indices is
     *  shown for duration seconds. */
    static SpriteAnimation uniform(SpriteSheetId sheet, std::vector<std::uint32_t> indices,
                                   float duration, bool loop = true);
};

/** Advances every SpriteAnimation in scene by deltaSeconds. */
void advanceAnimations(Scene& scene, float deltaSeconds);

} // namespace engine
