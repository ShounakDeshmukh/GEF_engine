#pragma once

// Game-specific networked events shared by the networked_events_demo server and client. Each
// has a kType (its message type on the wire) and encode()/decode(), which makes it an
// engine::events::networkedEvent: subscribing to it lets eventManager::receive() decode it.
//
// Spawning, dying and colliding use the engine's common events (spawnEvent, deathEvent,
// collisionEvent in engine/events/commonEvents.hpp); only what they don't cover is here.

#include <cstdint>
#include <cstring>
#include <engine/events/simpleEvent.hpp>
#include <optional>

namespace demo {

using engine::events::simpleEvent;
using engine::networking::Bytes;
using engine::networking::ByteView;

constexpr float worldWidth = 1280.f;
constexpr float worldHeight = 720.f;
constexpr float playerSize = 48.f;

// Fixed-size payloads: copies the listed fields one after another.
template <typename... Fields> Bytes pack(const Fields&... fields) {
    Bytes out;
    (
        [&] {
            const auto at = out.size();
            out.resize(at + sizeof(Fields));
            std::memcpy(out.data() + at, &fields, sizeof(Fields));
        }(),
        ...);
    return out;
}

template <typename... Fields> bool unpack(ByteView in, Fields&... fields) {
    if (in.size() != (sizeof(Fields) + ... + 0)) {
        return false;
    }
    std::size_t at = 0;
    (
        [&] {
            std::memcpy(&fields, in.data() + at, sizeof(Fields));
            at += sizeof(Fields);
        }(),
        ...);
    return true;
}

// client -> server: the direction the sender wants its player to move, each axis -1, 0 or 1.
struct moveRequest : simpleEvent {
    static constexpr std::uint16_t kType = 1;
    std::int8_t x = 0;
    std::int8_t y = 0;

    Bytes encode() const { return pack(x, y); }
    static std::optional<moveRequest> decode(ByteView in) {
        moveRequest e;
        return unpack(in, e.x, e.y) ? std::optional(e) : std::nullopt;
    }
};

// server -> clients: owner's player is gone.
struct playerLeft : simpleEvent {
    static constexpr std::uint16_t kType = 2;
    engine::ClientId owner = 0;

    Bytes encode() const { return pack(owner); }
    static std::optional<playerLeft> decode(ByteView in) {
        playerLeft e;
        return unpack(in, e.owner) ? std::optional(e) : std::nullopt;
    }
};

} // namespace demo
