#include <catch2/catch_test_macros.hpp>
#include <engine/inputHandler.hpp>

TEST_CASE("InputHandler::isKeyPressed reports false with no simulated input", "[input]") {
    engine::InputHandler input;

    REQUIRE_FALSE(input.isKeyPressed(engine::SC::SDL_SCANCODE_A));
    REQUIRE_FALSE(input.isKeyPressed(engine::SC::SDL_SCANCODE_W));
    REQUIRE_FALSE(input.isKeyPressed(engine::SC::SDL_SCANCODE_SPACE));
}

TEST_CASE("InputHandler::isKeyPressed handles SDL_SCANCODE_UNKNOWN safely", "[input]") {
    engine::InputHandler input;

    REQUIRE_FALSE(input.isKeyPressed(engine::SC::SDL_SCANCODE_UNKNOWN));
}

TEST_CASE("InputHandler::isKeyPressed returns false for out-of-range scancodes", "[input]") {
    engine::InputHandler input;

    const auto farOutOfRange = static_cast<engine::SC::SDL_Scancode>(100000);
    REQUIRE_FALSE(input.isKeyPressed(farOutOfRange));
}

TEST_CASE("InputHandler::isKeyPressed rejects the boundary scancode without reading out of bounds",
          "[input]") {
    engine::InputHandler input;

    const auto boundary = static_cast<engine::SC::SDL_Scancode>(engine::SC::SDL_SCANCODE_COUNT);
    REQUIRE_FALSE(input.isKeyPressed(boundary));
}

TEST_CASE("InputHandler::isKeyPressed is stable across repeated calls", "[input]") {
    engine::InputHandler input;

    const bool first = input.isKeyPressed(engine::SC::SDL_SCANCODE_D);
    const bool second = input.isKeyPressed(engine::SC::SDL_SCANCODE_D);

    REQUIRE(first == second);
}
