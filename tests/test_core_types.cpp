#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <engine/ids.hpp>
#include <engine/networking/bytes.hpp>
#include <type_traits>

static_assert(engine::ownerOf(engine::makeNetId(3, 7)) == 3);
static_assert(engine::localOf(engine::makeNetId(3, 7)) == 7);
static_assert(engine::makeNetId(engine::kServerId, 1) == 1);
static_assert(engine::makeNetId(engine::kMaxClientIdPerRun, engine::kNetIdLocalMask)
               == 0xFFFFFFFFu);
static_assert(std::is_convertible_v<const engine::networking::Bytes&,
                                     engine::networking::ByteView>);

TEST_CASE("NetId round trip recovers owner and local across ranges", "[ids]") {
    for (const engine::ClientId owner : {0u, 1u, 2u, 128u, 255u}) {
        for (const std::uint32_t local : {0u, 1u, 0xFFFFu, engine::kNetIdLocalMask}) {
            const engine::NetId id = engine::makeNetId(owner, local);
            REQUIRE(engine::ownerOf(id) == owner);
            REQUIRE(engine::localOf(id) == local);
        }
    }
}

TEST_CASE("NetId differs across owners for the same local value", "[ids]") {
    const std::uint32_t local = 42;
    const engine::NetId a = engine::makeNetId(1, local);
    const engine::NetId b = engine::makeNetId(2, local);
    REQUIRE(a != b);
}

TEST_CASE("ByteView built from Bytes shares size and data pointer", "[bytes]") {
    const engine::networking::Bytes bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    const engine::networking::ByteView view = bytes;
    REQUIRE(view.size() == 3);
    REQUIRE(view.data() == bytes.data());
}
