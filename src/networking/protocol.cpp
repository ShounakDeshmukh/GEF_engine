#include "engine/networking/protocol.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <limits>

namespace engine::networking {
namespace {
void append(Bytes& out, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        out.push_back(std::byte((value >> (i * 8)) & 255));
}
void appendBlob(Bytes& out, ByteView bytes) {
    append(out, bytes.size(), 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
}
struct Reader {
    ByteView bytes;
    std::size_t offset = 0;
    std::optional<std::uint64_t> number(unsigned count) {
        if (bytes.size() - offset < count)
            return std::nullopt;
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(std::to_integer<unsigned>(bytes[offset++])) << (i * 8);
        return value;
    }
    std::optional<Bytes> blob() {
        const auto size = number(4);
        if (!size || *size > bytes.size() - offset)
            return std::nullopt;
        Bytes result(bytes.begin() + offset, bytes.begin() + offset + *size);
        offset += *size;
        return result;
    }
    bool done() const { return offset == bytes.size(); }
};
bool validId(ClientId id) {
    return id > 0 && id <= kMaxClientIdPerRun;
}
bool validType(MessageType type) {
    return type >= MessageType::Join && type <= MessageType::Leave;
}
} // namespace
bool validPeerEndpoint(const std::string& endpoint) {
    if (!endpoint.starts_with("tcp://") || endpoint.size() > 255 ||
        endpoint.find('\0') != std::string::npos)
        return false;
    const auto colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon <= 6)
        return false;
    const auto host = endpoint.substr(6, colon - 6);
    if (host.find('*') != std::string::npos || host.find_first_of(" /\t\r\n") != std::string::npos)
        return false;
    unsigned port = 0;
    const auto end = endpoint.data() + endpoint.size();
    const auto parsed = std::from_chars(endpoint.data() + colon + 1, end, port);
    return parsed.ec == std::errc{} && parsed.ptr == end && port > 0 && port <= 65535;
}
Bytes encode(const Packet& packet) {
    if (!validType(packet.type) || packet.sender > kMaxClientIdPerRun ||
        packet.payload.size() > Packet::maxPayloadSize)
        return {};
    Bytes out;
    out.reserve(Packet::headerSize + packet.payload.size());
    append(out, Packet::magic, 2);
    append(out, Packet::version, 1);
    append(out, static_cast<unsigned>(packet.type), 1);
    append(out, packet.sender, 4);
    append(out, packet.sequence, 4);
    append(out, std::bit_cast<std::uint64_t>(packet.tick), 8);
    appendBlob(out, packet.payload);
    return out;
}
std::optional<Packet> decode(ByteView bytes) {
    if (bytes.size() < Packet::headerSize ||
        bytes.size() > Packet::headerSize + Packet::maxPayloadSize)
        return std::nullopt;
    Reader r{bytes};
    const auto magic = r.number(2), version = r.number(1), type = r.number(1);
    const auto sender = r.number(4), sequence = r.number(4), tick = r.number(8);
    auto payload = r.blob();
    if (!magic || *magic != Packet::magic || !version || *version != Packet::version || !type ||
        !validType(static_cast<MessageType>(*type)) || !sender || *sender > kMaxClientIdPerRun ||
        !sequence || !tick || !payload || !r.done())
        return std::nullopt;
    return Packet{static_cast<MessageType>(*type), static_cast<ClientId>(*sender),
                  static_cast<std::uint32_t>(*sequence), std::bit_cast<std::int64_t>(*tick),
                  std::move(*payload)};
}
Bytes encodeServerUpdate(const ServerUpdate& update) {
    if (update.world.size() > Packet::maxPayloadSize || update.peers.size() > 255 ||
        update.players.size() > 255)
        return {};
    Bytes out;
    append(out, std::bit_cast<std::uint64_t>(update.worldTick), 8);
    appendBlob(out, update.world);
    append(out, update.players.size(), 1);
    std::array<bool, 256> players{};
    for (const auto& player : update.players) {
        if (!validId(player.client) || players[player.client] ||
            player.payload.size() > Packet::maxPayloadSize)
            return {};
        players[player.client] = true;
        append(out, player.client, 4);
        append(out, std::bit_cast<std::uint64_t>(player.tick), 8);
        appendBlob(out, player.payload);
        if (out.size() > Packet::maxPayloadSize)
            return {};
    }
    append(out, update.peers.size(), 1);
    std::array<bool, 256> peers{};
    for (const auto& peer : update.peers) {
        if (!validId(peer.id) || peers[peer.id] ||
            (!peer.endpoint.empty() && !validPeerEndpoint(peer.endpoint)))
            return {};
        peers[peer.id] = true;
        append(out, peer.id, 4);
        append(out, peer.endpoint.size(), 1);
        for (unsigned char c : peer.endpoint)
            out.push_back(std::byte(c));
    }
    for (const auto& player : update.players)
        if (!peers[player.client])
            return {};
    return out.size() <= Packet::maxPayloadSize ? out : Bytes{};
}
std::optional<ServerUpdate> decodeServerUpdate(ByteView bytes) {
    if (bytes.size() > Packet::maxPayloadSize)
        return std::nullopt;
    Reader r{bytes};
    const auto tick = r.number(8);
    auto world = r.blob();
    const auto playerCount = r.number(1);
    if (!tick || !world || !playerCount)
        return std::nullopt;
    ServerUpdate result{std::bit_cast<std::int64_t>(*tick), std::move(*world), {}, {}};
    std::array<bool, 256> players{}, peers{};
    for (unsigned i = 0; i < *playerCount; ++i) {
        const auto id = r.number(4), playerTick = r.number(8);
        auto payload = r.blob();
        if (!id || !validId(*id) || players[*id] || !playerTick || !payload)
            return std::nullopt;
        players[*id] = true;
        result.players.push_back({static_cast<ClientId>(*id),
                                  std::bit_cast<std::int64_t>(*playerTick), std::move(*payload)});
    }
    const auto peerCount = r.number(1);
    if (!peerCount)
        return std::nullopt;
    for (unsigned i = 0; i < *peerCount; ++i) {
        const auto id = r.number(4), size = r.number(1);
        if (!id || !validId(*id) || peers[*id] || !size || *size > bytes.size() - r.offset)
            return std::nullopt;
        peers[*id] = true;
        std::string endpoint;
        for (unsigned j = 0; j < *size; ++j)
            endpoint.push_back(
                static_cast<char>(std::to_integer<unsigned char>(bytes[r.offset++])));
        if (!endpoint.empty() && !validPeerEndpoint(endpoint))
            return std::nullopt;
        result.peers.push_back({static_cast<ClientId>(*id), std::move(endpoint)});
    }
    for (const auto& player : result.players)
        if (!peers[player.client])
            return std::nullopt;
    if (!r.done())
        return std::nullopt;
    return result;
}
Bytes encodeWelcome(const Welcome& welcome) {
    if (!validId(welcome.client) || welcome.port == 0)
        return {};
    auto update = encodeServerUpdate(welcome.update);
    if (update.empty() || update.size() + 6 > Packet::maxPayloadSize)
        return {};
    Bytes out;
    append(out, welcome.client, 4);
    append(out, welcome.port, 2);
    out.insert(out.end(), update.begin(), update.end());
    return out;
}
std::optional<Welcome> decodeWelcome(ByteView bytes) {
    if (bytes.size() < 6 || bytes.size() > Packet::maxPayloadSize)
        return std::nullopt;
    Reader r{bytes};
    const auto id = r.number(4), port = r.number(2);
    if (!id || !validId(*id) || !port || *port == 0)
        return std::nullopt;
    auto update = decodeServerUpdate(bytes.subspan(r.offset));
    if (!update)
        return std::nullopt;
    return Welcome{static_cast<ClientId>(*id), static_cast<std::uint16_t>(*port),
                   std::move(*update)};
}
} // namespace engine::networking
