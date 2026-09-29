#include "engine/keyboardState.hpp"

#include "engine/inputHandler.hpp"

namespace engine {
namespace {

// Cast first: the enum's underlying type is implementation-defined and may be unsigned.
bool inRange(SC::SDL_Scancode scancode) noexcept {
    const int index = static_cast<int>(scancode);
    return index >= 0 && index < SC::SDL_SCANCODE_COUNT;
}

} // namespace

KeyboardState KeyboardState::capture(InputHandler& handler) {
    KeyboardState snapshot;
    for (int i = 0; i < SC::SDL_SCANCODE_COUNT; ++i) {
        snapshot.keys_[i] = handler.isKeyPressed(static_cast<SC::SDL_Scancode>(i));
    }
    return snapshot;
}

bool KeyboardState::isKeyPressed(SC::SDL_Scancode scancode) const noexcept {
    return inRange(scancode) && keys_[scancode];
}

bool KeyboardState::justPressed(SC::SDL_Scancode scancode,
                                const KeyboardState& previous) const noexcept {
    return isKeyPressed(scancode) && !previous.isKeyPressed(scancode);
}

void KeyboardState::setKey(SC::SDL_Scancode scancode, bool pressed) noexcept {
    if (inRange(scancode)) {
        keys_[scancode] = pressed;
    }
}

} // namespace engine
