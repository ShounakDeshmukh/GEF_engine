#pragma once

/** @file
 *  Byte buffer types shared by the transport and codec layers. Transport code
 *  moves Bytes/ByteView only and never interprets message contents.
 */

#include <cstddef>
#include <span>
#include <vector>

namespace engine::networking {
    /** Owning buffer for an encoded message or payload. */
    using Bytes = std::vector<std::byte>;

    /** Read-only view of bytes owned by the caller; it does not extend their lifetime. */
    using ByteView = std::span<const std::byte>;
}
