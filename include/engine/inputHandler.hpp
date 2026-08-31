#pragma once

#include <unordered_map>

namespace engine
{
    class InputHandler {
    
        public:
        InputHandler();
        ~InputHandler(){}
        InputHandler(const InputHandler& other);
        InputHandler& operator=(const InputHandler& other);
        InputHandler(InputHandler&& other);
        InputHandler& operator=(InputHandler&& other);
    
    
        bool IsKeyPressed(const char val);
    
        private:
            std::unordered_map<char, int> m_charToScanCodeMap;
    
    
    };
    
} // namespace engine

