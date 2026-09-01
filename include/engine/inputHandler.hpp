#pragma once

#include "engine/inputScancodes.hpp"

#include <unordered_map>

namespace engine {
class InputHandler {

public:
    InputHandler() {}
    ~InputHandler() {}

    bool isKeyPressed(SC::SDL_Scancode scancode);

private:
};

} // namespace engine
