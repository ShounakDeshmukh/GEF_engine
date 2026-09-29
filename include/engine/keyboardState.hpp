#pragma once

#include "engine/inputScancodes.hpp"

#include <bitset>

namespace engine {

class InputHandler;

/** Value copy of keyboard state, so threads other than main never read SDL's keyboard array.
 *
 *  @thread_safety Not thread-safe; hand copies between threads instead. */
class KeyboardState {
public:
    /** Main thread only: SDL updates the keyboard array during Window::pollEvents(). */
    static KeyboardState capture(InputHandler& handler);

    /** False for out-of-range scancodes, like InputHandler::isKeyPressed. */
    bool isKeyPressed(SC::SDL_Scancode scancode) const noexcept;

    /** True if pressed here and not in previous. */
    bool justPressed(SC::SDL_Scancode scancode, const KeyboardState& previous) const noexcept;

    /** A no-op for out-of-range scancodes. For tests and synthetic input. */
    void setKey(SC::SDL_Scancode scancode, bool pressed) noexcept;

    bool operator==(const KeyboardState&) const = default;

private:
    std::bitset<SC::SDL_SCANCODE_COUNT> keys_;
};

} // namespace engine
