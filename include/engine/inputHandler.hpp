#pragma once

#include <unordered_map>
#include "engine/inputScancodes.hpp"

namespace engine
{
    class InputHandler {
    
        public:
        InputHandler(){}
        ~InputHandler(){}
    
        bool IsKeyPressed(SC::SDL_Scancode scancode);
        
        private:

    
    };


    
} // namespace engine

