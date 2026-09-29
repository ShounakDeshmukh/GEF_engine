#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <engine/networking/protocol.hpp>
#include <limits>

TEST_CASE("network packet round-trips its header and payload", "[networking][protocol]") {
    const engine::networking::Packet packet{
        .type = engine::networking::MessageType::PlayerState,
        .sender = 7,
        .sequence = 42,
        .serverTick = 123456,
        .payload = {std::byte{1}, std::byte{2}, std::byte{255}},
    };

    const engine::networking::Bytes encoded = engine::networking::encode(packet);
    const auto decoded = engine::networking::decode(encoded);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->type == packet.type);
    REQUIRE(decoded->sender == packet.sender);
    REQUIRE(decoded->sequence == packet.sequence);
    REQUIRE(decoded->serverTick == packet.serverTick);
    REQUIRE(decoded->payload.size() == packet.payload.size());
    REQUIRE(std::equal(decoded->payload.begin(), decoded->payload.end(),
                       packet.payload.begin(), packet.payload.end()));
}

TEST_CASE("network packet decoder rejects malformed input", "[networking][protocol]") {
    const engine::networking::Packet packet{
        .type = engine::networking::MessageType::Ping,
        .sender = 1,
        .sequence = 2,
        .serverTick = 3,
        .payload = {std::byte{9}},
    };
    const engine::networking::Bytes encoded = engine::networking::encode(packet);

    REQUIRE_FALSE(engine::networking::decode({}).has_value());
    REQUIRE_FALSE(
        engine::networking::decode(engine::networking::ByteView(encoded.data(), encoded.size() - 1))
            .has_value());

    auto invalid = encoded;
    invalid[0] = std::byte{0};
    REQUIRE_FALSE(engine::networking::decode(invalid).has_value());

    invalid = encoded;
    invalid[3] = std::byte{0};
    REQUIRE_FALSE(engine::networking::decode(invalid).has_value());
}

TEST_CASE("network packet encoder rejects oversized payloads", "[networking][protocol]") {
    engine::networking::Packet packet{
        .type = engine::networking::MessageType::Ping,
        .sender = 1,
        .sequence = 2,
        .serverTick = 3,
        .payload = engine::networking::Bytes(engine::networking::Packet::maxPayloadSize + 1),
    };

    REQUIRE(engine::networking::encode(packet).empty());
}

TEST_CASE("position payload rejects truncated and nonfinite values", "[networking][protocol]") {
    const auto encoded = engine::networking::encodePosition({120.5f, -30.f});
    const auto position = engine::networking::decodePosition(encoded);
    REQUIRE(position);
    REQUIRE(position->x == 120.5f);
    REQUIRE(position->y == -30.f);
    REQUIRE_FALSE(engine::networking::decodePosition({encoded.data(), 7}));
    REQUIRE(
        engine::networking::encodePosition({std::numeric_limits<float>::infinity(), 1.f}).empty());
    auto invalid = encoded;
    invalid[3] = std::byte{0x7f};
    invalid[2] = std::byte{0x80};
    invalid[1] = std::byte{0};
    invalid[0] = std::byte{0};
    REQUIRE_FALSE(engine::networking::decodePosition(invalid));
}

TEST_CASE("world snapshot round trips peers and rejects truncation", "[networking][protocol]") {
    engine::networking::WorldSnapshot world{
        42, 1100.f, 440.f, {{1, "tcp://127.0.0.1:6101"}, {2, "tcp://127.0.0.1:6102"}}};
    const auto encoded = engine::networking::encodeWorld(world);
    const auto decoded = engine::networking::decodeWorld(encoded);
    REQUIRE(decoded);
    REQUIRE(decoded->tick == 42);
    REQUIRE(decoded->droneX == 1100.f);
    REQUIRE(decoded->droneY == 440.f);
    REQUIRE(decoded->peers.size() == 2);
    REQUIRE(decoded->peers[1].id == 2);
    REQUIRE(decoded->peers[1].endpoint == "tcp://127.0.0.1:6102");
    REQUIRE_FALSE(engine::networking::decodeWorld({encoded.data(), encoded.size() - 1}));

    world.peers[0].endpoint = std::string("tcp://host:", 11) + '\0' + "6101";
    REQUIRE(engine::networking::encodeWorld(world).empty());
}
