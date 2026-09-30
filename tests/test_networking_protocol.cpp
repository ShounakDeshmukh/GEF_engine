#include <catch2/catch_test_macros.hpp>
#include <engine/networking/protocol.hpp>
#include <limits>

using namespace engine::networking;

TEST_CASE("network packets preserve generic binary data and signed ticks",
          "[networking][protocol]") {
    const Packet packet{
        MessageType::PlayerState, 7, 42, -123, {std::byte{0}, std::byte{255}, std::byte{12}}};
    const auto bytes = encode(packet);
    REQUIRE(bytes.size() == Packet::headerSize + packet.payload.size());
    const auto copy = decode(bytes);
    REQUIRE(copy);
    CHECK(copy->type == packet.type);
    CHECK(copy->sender == packet.sender);
    CHECK(copy->sequence == packet.sequence);
    CHECK(copy->tick == packet.tick);
    CHECK(copy->payload == packet.payload);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        CHECK_FALSE(decode(ByteView(bytes).first(size)));
    auto invalid = bytes;
    invalid.push_back(std::byte{0});
    CHECK_FALSE(decode(invalid));
    invalid = bytes;
    invalid[2] = std::byte{1};
    CHECK_FALSE(decode(invalid)); // Old message numbers must not be interpreted as version 2.
    invalid = bytes;
    invalid[3] = std::byte{255};
    CHECK_FALSE(decode(invalid));
    auto large = packet;
    large.payload.resize(Packet::maxPayloadSize);
    REQUIRE(decode(encode(large)));
    large.payload.push_back(std::byte{0});
    CHECK(encode(large).empty());
}
TEST_CASE("world and welcome codecs round trip opaque state and directory",
          "[networking][protocol]") {
    ServerUpdate update{-100,
                        {std::byte{3}, std::byte{0}},
                        {{1, -30, {std::byte{99}}}, {2, 40, {}}},
                        {{1, ""}, {2, "tcp://127.0.0.1:6102"}}};
    const auto bytes = encodeServerUpdate(update);
    REQUIRE_FALSE(bytes.empty());
    const auto copy = decodeServerUpdate(bytes);
    REQUIRE(copy);
    CHECK(copy->worldTick == -100);
    CHECK(copy->world == update.world);
    REQUIRE(copy->players.size() == 2);
    CHECK(copy->players[0].payload == update.players[0].payload);
    CHECK(copy->players[0].tick == -30);
    CHECK(copy->players[1].payload.empty());
    REQUIRE(copy->peers.size() == 2);
    CHECK(copy->peers[1].endpoint == update.peers[1].endpoint);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        CHECK_FALSE(decodeServerUpdate(ByteView(bytes).first(size)));
    auto invalid = bytes;
    invalid.push_back(std::byte{0});
    CHECK_FALSE(decodeServerUpdate(invalid));
    const auto welcome = encodeWelcome({3, 65432, update});
    const auto restored = decodeWelcome(welcome);
    REQUIRE(restored);
    CHECK(restored->client == 3);
    CHECK(restored->port == 65432);
    CHECK(restored->update.world == update.world);
    for (std::size_t size = 0; size < welcome.size(); ++size)
        CHECK_FALSE(decodeWelcome(ByteView(welcome).first(size)));
    update.peers.push_back(update.peers.front());
    CHECK(encodeServerUpdate(update).empty());
    update.peers.pop_back();
    update.peers[1].endpoint = std::string("tcp://host:") + '\0' + "6101";
    CHECK(encodeServerUpdate(update).empty());
    update.peers[1].endpoint.clear();
    update.world.resize(Packet::maxPayloadSize);
    CHECK(encodeServerUpdate(update).empty());
    CHECK_FALSE(validPeerEndpoint("tcp://*:0"));
    CHECK_FALSE(validPeerEndpoint("tcp://host:99999"));
    CHECK_FALSE(validPeerEndpoint("tcp://host:12garbage"));
    CHECK(validPeerEndpoint("tcp://127.0.0.1:34567"));
}
