#pragma once

#include "bytes.hpp"
#include "engine/ids.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::networking {
// Version 2 uses generic game payloads and a different message set from the old position demo.
enum class MessageType : std::uint8_t {
    Join = 1,
    Welcome = 2,
    ClientUpdate = 3,
    ServerUpdate = 4,
    PlayerState = 5,
    Leave = 6
};
struct Packet {
    static constexpr std::uint16_t magic = 0x4745;
    static constexpr std::uint8_t version = 2;
    static constexpr std::size_t headerSize = 24;
    static constexpr std::size_t maxPayloadSize = 64 * 1024;
    MessageType type;
    ClientId sender = 0;
    std::uint32_t sequence = 0;
    std::int64_t tick = 0;
    Bytes payload;
};
struct PeerInfo {
    ClientId id;
    // Empty for a client using server relay; otherwise a reachable tcp://host:port address.
    std::string endpoint;
};
struct PlayerUpdate {
    ClientId client;
    std::int64_t tick;
    Bytes payload;
};
struct ServerUpdate {
    std::int64_t worldTick = 0;
    Bytes world;
    std::vector<PlayerUpdate> players;
    std::vector<PeerInfo> peers;
};
struct Welcome {
    ClientId client;
    std::uint16_t port;
    ServerUpdate update;
};

/** Fixed little-endian encoding. Invalid/oversized values return an empty buffer. */
Bytes encode(const Packet& packet);
Bytes encodeServerUpdate(const ServerUpdate& update);
Bytes encodeWelcome(const Welcome& welcome);
/** Malformed, truncated, trailing or oversized input returns nullopt. */
std::optional<Packet> decode(ByteView bytes);
std::optional<ServerUpdate> decodeServerUpdate(ByteView bytes);
std::optional<Welcome> decodeWelcome(ByteView bytes);
/** Rejects wildcard/empty hosts, invalid ports and embedded NULs in advertised peer addresses. */
bool validPeerEndpoint(const std::string& endpoint);
} // namespace engine::networking
