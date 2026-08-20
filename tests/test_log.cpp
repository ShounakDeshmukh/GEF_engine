#include <catch2/catch_test_macros.hpp>
#include <engine/log.hpp>

TEST_CASE("engine::log facade does not throw", "[log]") {
    engine::log::init();
    REQUIRE_NOTHROW(engine::log::info("plain string"));
    REQUIRE_NOTHROW(engine::log::warn("formatted value: {}", 42));
    REQUIRE_NOTHROW(engine::log::error("multi {} {}", "arg", 3.14));
}
