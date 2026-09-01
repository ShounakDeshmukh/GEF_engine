#include "engine/inputHandler.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

namespace engine {

bool InputHandler::isKeyPressed(SC::SDL_Scancode scancode) {
    // Should be threadsafe as its only doing reads
    int keyboardsize = 0;
    auto sdlKeyState = SDL_GetKeyboardState(&keyboardsize);

    if (static_cast<int>(scancode) > keyboardsize) {
        // Key not found
        return false;
    }

    return sdlKeyState[scancode];
}

} // namespace engine
