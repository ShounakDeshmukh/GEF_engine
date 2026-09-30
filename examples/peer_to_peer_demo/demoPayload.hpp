#pragma once

#include <engine/networking/bytes.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

// Game data stays outside the engine protocol. Each update is two little-endian floats.
namespace netdemo {
struct Position {
    float x, y;
};

inline engine::networking::Bytes encode(Position position) {
    engine::networking::Bytes bytes;
    bytes.reserve(8);
    for (const float value : {position.x, position.y}) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(std::byte((bits >> (8 * i)) & 0xffu));
    }
    return bytes;
}

inline std::optional<Position> decode(engine::networking::ByteView bytes) {
    if (bytes.size() != 8)
        return std::nullopt;
    float values[2];
    for (unsigned n = 0; n < 2; ++n) {
        std::uint32_t bits = 0;
        for (unsigned i = 0; i < 4; ++i)
            bits |= std::to_integer<std::uint32_t>(bytes[n * 4 + i]) << (8 * i);
        values[n] = std::bit_cast<float>(bits);
        if (!std::isfinite(values[n]))
            return std::nullopt;
    }
    return Position{values[0], values[1]};
}
} // namespace netdemo
