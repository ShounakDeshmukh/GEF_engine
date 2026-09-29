#pragma once

/** @file
 *  Wire types and codecs for peer registration, shared world state, and direct
 *  player updates. The codecs do not perform network I/O.
 */

#include "engine/ids.hpp"
#include "engine/networking/bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::networking {

/** Packet kinds in version 1 of the peer-to-peer protocol. */
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

/** A decoded wire packet. Multi-byte header fields use little-endian order.
 *  The 24-byte header contains a magic value, version, type, sender, sequence,
 *  server tick, and payload length.
 */
struct Packet {
    /** Signature checked by decode(). */
    static constexpr std::uint16_t magic = 0x4745;
    /** Supported wire-format version. */
    static constexpr std::uint8_t version = 1;
    /** Encoded header size in bytes. */
    static constexpr std::size_t headerSize = 24;
    /** Maximum payload accepted by the packet codec. */
    static constexpr std::size_t maxPayloadSize = 64 * 1024;

    /** Kind of message carried by this packet. */
    MessageType type;
    /** Client ID of the sender, or kServerId for server messages. */
    ClientId sender;
    /** Sequence number used to reject older direct player updates. */
    std::uint32_t sequence;
    /** Server tick associated with this packet, when applicable. */
    std::uint64_t serverTick;
    /** Message-specific data. */
    Bytes payload;
};

/** Encodes a packet using the fixed little-endian wire format.
 *  @return Header and payload, or an empty buffer if the payload exceeds
 *  Packet::maxPayloadSize.
 */
Bytes encode(const Packet& packet);

/** Decodes exactly one complete packet.
 *  @return The packet, or std::nullopt for a truncated packet, trailing bytes,
 *  unsupported version or type, or an oversized payload.
 */
std::optional<Packet> decode(ByteView bytes);

/** A registered peer and the TCP endpoint it advertises to other peers. */
struct PeerInfo {
    /** ID assigned by the coordinator. */
    ClientId id;
    /** Address other peers use to connect to this peer. */
    std::string endpoint;
};

/** Two-dimensional player position sent directly between peers. */
struct PlayerPosition {
    float x;
    float y;
};

/** Shared state and peer directory returned by the coordinator. */
struct WorldSnapshot {
    /** Coordinator tick for the shared state. */
    std::uint64_t tick;
    /** Shared drone position. */
    float droneX;
    float droneY;
    /** Currently active peers, including the recipient. */
    std::vector<PeerInfo> peers;
};

/** Encodes two finite floats as eight little-endian bytes.
 *  @return The payload, or an empty buffer if either coordinate is nonfinite.
 */
Bytes encodePosition(PlayerPosition position);

/** Decodes a player position from exactly eight bytes.
 *  @return The position, or std::nullopt for an invalid size or nonfinite value.
 */
std::optional<PlayerPosition> decodePosition(ByteView bytes);

/** Encodes shared state and at most 255 peers.
 *  @return The payload, or an empty buffer for nonfinite coordinates, peer IDs
 *  outside 1 to 255, empty or overlong endpoints, or a payload over
 *  Packet::maxPayloadSize.
 */
Bytes encodeWorld(const WorldSnapshot& world);

/** Decodes one complete shared-state payload.
 *  @return The snapshot, or std::nullopt for malformed or trailing data,
 *  nonfinite coordinates, or invalid peer entries.
 */
std::optional<WorldSnapshot> decodeWorld(ByteView bytes);

} // namespace engine::networking
