#pragma once

/** @file
 *  Byte buffer types shared by the transport and codec layers. Transport code
 *  moves Bytes/ByteView only and never interprets message contents.
 */

#include <cstddef>
#include <span>
#include <vector>

namespace engine::networking {
    using Bytes = std::vector<std::byte>;
    using ByteView = std::span<const std::byte>;
}
