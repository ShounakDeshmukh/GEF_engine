#pragma once

#include <cassert>
#include <cstdint>

namespace engine {
    /** Opaque handle to an entity. Values are assigned by Scene::createEntity() and are
     *  never reused for the lifetime of a Scene. Process-local; never sent over the
     *  network (see NetId). */
    using EntityId = std::uint32_t;

    /** Identifies a connected process. 0 is the server; clients start at 1 and are
     *  never reused within one server run, so at most 255 clients can join per run. */
    using ClientId = std::uint32_t;

    /** Network-wide entity identity. The owner ClientId is in the high 8 bits and an
     *  owner-assigned counter in the low 24, so owners allocate ids without
     *  coordination. */
    using NetId = std::uint32_t;

    inline constexpr ClientId kServerId = 0;
    inline constexpr ClientId kMaxClientIdPerRun = 255;
    inline constexpr std::uint32_t kNetIdLocalBits = 24;
    inline constexpr std::uint32_t kNetIdLocalMask = (1u << kNetIdLocalBits) - 1;

    constexpr NetId makeNetId(ClientId owner, std::uint32_t local) noexcept {
        assert(owner <= kMaxClientIdPerRun);
        assert(local <= kNetIdLocalMask);
        return (static_cast<NetId>(owner) << kNetIdLocalBits) | local;
    }

    constexpr ClientId ownerOf(NetId id) noexcept {
        return static_cast<ClientId>(id >> kNetIdLocalBits);
    }

    constexpr std::uint32_t localOf(NetId id) noexcept {
        return id & kNetIdLocalMask;
    }
}
