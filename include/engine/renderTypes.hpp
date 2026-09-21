#pragma once

#include <cstdint>

#include "engine/ids.hpp"

namespace engine {
    /** Handle to a texture loaded by Renderer::loadTexture. */
    using TextureId = std::uint32_t;

    /** Handle to a texture loaded by Renderer::loadFont. */
    using FontId = std::uint32_t;

    /** RGBA color, 0-255 per channel. */
    struct Color {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
    };

}