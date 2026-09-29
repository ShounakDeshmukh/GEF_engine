#include "engine/networking/protocol.hpp"

#include <bit>
#include <cmath>
#include <utility>

namespace engine::networking {
namespace {

void append8(Bytes& output, std::uint8_t value) {
    output.push_back(static_cast<std::byte>(value));
}

void append16(Bytes& output, std::uint16_t value) {
    append8(output, static_cast<std::uint8_t>(value));
    append8(output, static_cast<std::uint8_t>(value >> 8));
}

void append32(Bytes& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        append8(output, static_cast<std::uint8_t>(value >> shift));
    }
}

void append64(Bytes& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        append8(output, static_cast<std::uint8_t>(value >> shift));
    }
}

class Reader {
public:
    explicit Reader(ByteView bytes) : bytes_(bytes) {}

    std::optional<std::uint8_t> read8() {
        if (position_ >= bytes_.size()) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }

    std::optional<std::uint16_t> read16() {
        const auto low = read8();
        const auto high = read8();
        if (!low || !high) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(*low) | (static_cast<std::uint16_t>(*high) << 8);
    }

    std::optional<std::uint32_t> read32() {
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const auto byte = read8();
            if (!byte) {
                return std::nullopt;
            }
            value |= static_cast<std::uint32_t>(*byte) << shift;
        }
        return value;
    }

    std::optional<std::uint64_t> read64() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            const auto byte = read8();
            if (!byte) {
                return std::nullopt;
            }
            value |= static_cast<std::uint64_t>(*byte) << shift;
        }
        return value;
    }

    std::optional<Bytes> readBytes(std::size_t count) {
        if (count > bytes_.size() - position_) {
            return std::nullopt;
        }
        Bytes result(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
        position_ += count;
        return result;
    }

private:
    ByteView bytes_;
    std::size_t position_ = 0;
};

} // namespace

Bytes encode(const Packet& packet) {
    if (packet.payload.size() > Packet::maxPayloadSize) {
        return {};
    }

    Bytes output;
    output.reserve(Packet::headerSize + packet.payload.size());
    append16(output, Packet::magic);
    append8(output, Packet::version);
    append8(output, static_cast<std::uint8_t>(packet.type));
    append32(output, packet.sender);
    append32(output, packet.sequence);
    append64(output, packet.serverTick);
    append32(output, static_cast<std::uint32_t>(packet.payload.size()));
    output.insert(output.end(), packet.payload.begin(), packet.payload.end());
    return output;
}

std::optional<Packet> decode(ByteView bytes) {
    if (bytes.size() < Packet::headerSize) {
        return std::nullopt;
    }

    Reader reader(bytes);
    const auto magic = reader.read16();
    const auto version = reader.read8();
    const auto type = reader.read8();
    const auto sender = reader.read32();
    const auto sequence = reader.read32();
    const auto serverTick = reader.read64();
    const auto payloadSize = reader.read32();
    if (!magic || !version || !type || !sender || !sequence || !serverTick || !payloadSize ||
        *magic != Packet::magic || *version != Packet::version || *type == 0 ||
        *type > static_cast<std::uint8_t>(MessageType::Pong) ||
        *payloadSize > Packet::maxPayloadSize ||
        bytes.size() != Packet::headerSize + static_cast<std::size_t>(*payloadSize)) {
        return std::nullopt;
    }

    const auto payload = reader.readBytes(*payloadSize);
    if (!payload) {
        return std::nullopt;
    }

    return Packet{static_cast<MessageType>(*type), *sender, *sequence, *serverTick,
                  std::move(*payload)};
}

Bytes encodePosition(PlayerPosition position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        return {};
    Bytes out;
    append32(out, std::bit_cast<std::uint32_t>(position.x));
    append32(out, std::bit_cast<std::uint32_t>(position.y));
    return out;
}

std::optional<PlayerPosition> decodePosition(ByteView bytes) {
    if (bytes.size() != 8)
        return std::nullopt;
    Reader reader(bytes);
    const auto x = reader.read32();
    const auto y = reader.read32();
    if (!x || !y)
        return std::nullopt;
    PlayerPosition result{std::bit_cast<float>(*x), std::bit_cast<float>(*y)};
    if (!std::isfinite(result.x) || !std::isfinite(result.y))
        return std::nullopt;
    return result;
}

Bytes encodeWorld(const WorldSnapshot& world) {
    if (!std::isfinite(world.droneX) || !std::isfinite(world.droneY) || world.peers.size() > 255)
        return {};
    Bytes out;
    append64(out, world.tick);
    append32(out, std::bit_cast<std::uint32_t>(world.droneX));
    append32(out, std::bit_cast<std::uint32_t>(world.droneY));
    append8(out, static_cast<std::uint8_t>(world.peers.size()));
    for (const auto& peer : world.peers) {
        if (peer.id == 0 || peer.id > 255 || peer.endpoint.empty() || peer.endpoint.size() > 255 ||
            peer.endpoint.find('\0') != std::string::npos)
            return {};
        append32(out, peer.id);
        append8(out, static_cast<std::uint8_t>(peer.endpoint.size()));
        for (char c : peer.endpoint)
            append8(out, static_cast<std::uint8_t>(c));
    }
    if (out.size() > Packet::maxPayloadSize)
        return {};
    return out;
}

std::optional<WorldSnapshot> decodeWorld(ByteView bytes) {
    Reader reader(bytes);
    const auto tick = reader.read64();
    const auto x = reader.read32();
    const auto y = reader.read32();
    const auto count = reader.read8();
    if (!tick || !x || !y || !count)
        return std::nullopt;
    WorldSnapshot result{*tick, std::bit_cast<float>(*x), std::bit_cast<float>(*y), {}};
    if (!std::isfinite(result.droneX) || !std::isfinite(result.droneY))
        return std::nullopt;
    for (unsigned i = 0; i < *count; ++i) {
        const auto id = reader.read32();
        const auto size = reader.read8();
        if (!id || !size || *id == 0 || *id > 255 || *size == 0)
            return std::nullopt;
        const auto endpoint = reader.readBytes(*size);
        if (!endpoint)
            return std::nullopt;
        std::string address;
        for (auto b : *endpoint)
            address.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
        if (address.find('\0') != std::string::npos)
            return std::nullopt;
        result.peers.push_back({*id, std::move(address)});
    }
    if (encodeWorld(result).size() != bytes.size())
        return std::nullopt;
    return result;
}

} // namespace engine::networking
