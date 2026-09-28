#pragma once

#include "engine/ids.hpp"
#include "engine/networking/bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::networking {

enum class MessageType : std::uint8_t {
    Register = 1,
    Welcome = 2,
    PeerList = 3,
    PeerJoined = 4,
    PeerLeft = 5,
    PlayerState = 6,
    SharedWorldState = 7,
    Ping = 8,
    Pong = 9,
};

struct Packet {
    static constexpr std::uint16_t magic = 0x4745;
    static constexpr std::uint8_t version = 1;
    static constexpr std::size_t headerSize = 24;
    static constexpr std::size_t maxPayloadSize = 64 * 1024;

    MessageType type;
    ClientId sender;
    std::uint32_t sequence;
    std::uint64_t serverTick;
    Bytes payload;
};

/** Encodes a packet using a fixed little-endian wire format. */
Bytes encode(const Packet& packet);

/** Decodes one complete packet, returning nullopt for invalid input. */
std::optional<Packet> decode(ByteView bytes);

struct PeerInfo {
    ClientId id;
    std::string endpoint;
};

struct PlayerPosition {
    float x;
    float y;
};

struct WorldSnapshot {
    std::uint64_t tick;
    float droneX;
    float droneY;
    std::vector<PeerInfo> peers;
};

Bytes encodePosition(PlayerPosition position);
std::optional<PlayerPosition> decodePosition(ByteView bytes);
Bytes encodeWorld(const WorldSnapshot& world);
std::optional<WorldSnapshot> decodeWorld(ByteView bytes);

} // namespace engine::networking
