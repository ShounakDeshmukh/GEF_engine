#include "engine/inputHandler.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

namespace engine {

std::unordered_map<char, int> GenerateScancodeMap() {
    std::unordered_map<char, int> retVal = {
        //lowercase 
        {'a', SDL_SCANCODE_A},
        {'b', SDL_SCANCODE_B},
        {'c', SDL_SCANCODE_C},
        {'d', SDL_SCANCODE_D},
        {'e', SDL_SCANCODE_E},
        {'f', SDL_SCANCODE_F},
        {'g', SDL_SCANCODE_G},
        {'h', SDL_SCANCODE_H},
        {'i', SDL_SCANCODE_I},
        {'j', SDL_SCANCODE_J},
        {'k', SDL_SCANCODE_K},
        {'l', SDL_SCANCODE_L},
        {'m', SDL_SCANCODE_M},
        {'n', SDL_SCANCODE_N},
        {'o', SDL_SCANCODE_O},
        {'p', SDL_SCANCODE_P},
        {'q', SDL_SCANCODE_Q},
        {'r', SDL_SCANCODE_R},
        {'s', SDL_SCANCODE_S},
        {'t', SDL_SCANCODE_T},
        {'u', SDL_SCANCODE_U},
        {'v', SDL_SCANCODE_V},
        {'w', SDL_SCANCODE_W},
        {'x', SDL_SCANCODE_X},
        {'y', SDL_SCANCODE_Y},
        {'z', SDL_SCANCODE_Z},  
        
        //Uppercase
        {'A', SDL_SCANCODE_A},
        {'B', SDL_SCANCODE_B},
        {'C', SDL_SCANCODE_C},
        {'D', SDL_SCANCODE_D},
        {'E', SDL_SCANCODE_E},
        {'F', SDL_SCANCODE_F},
        {'G', SDL_SCANCODE_G},
        {'H', SDL_SCANCODE_H},
        {'I', SDL_SCANCODE_I},
        {'J', SDL_SCANCODE_J},
        {'K', SDL_SCANCODE_K},
        {'L', SDL_SCANCODE_L},
        {'M', SDL_SCANCODE_M},
        {'N', SDL_SCANCODE_N},
        {'O', SDL_SCANCODE_O},
        {'P', SDL_SCANCODE_P},
        {'Q', SDL_SCANCODE_Q},
        {'R', SDL_SCANCODE_R},
        {'S', SDL_SCANCODE_S},
        {'T', SDL_SCANCODE_T},
        {'U', SDL_SCANCODE_U},
        {'V', SDL_SCANCODE_V},
        {'W', SDL_SCANCODE_W},
        {'X', SDL_SCANCODE_X},
        {'Y', SDL_SCANCODE_Y},
        {'Z', SDL_SCANCODE_Z},  

        //numberpad
        {'1', SDL_SCANCODE_1},
        {'2', SDL_SCANCODE_2},
        {'3', SDL_SCANCODE_3},
        {'4', SDL_SCANCODE_4},
        {'5', SDL_SCANCODE_5},
        {'6', SDL_SCANCODE_6},
        {'7', SDL_SCANCODE_7},
        {'8', SDL_SCANCODE_8},
        {'9', SDL_SCANCODE_9},
        {'0', SDL_SCANCODE_0}

    };
    return retVal;
}

InputHandler::InputHandler(){
    m_charToScanCodeMap = GenerateScancodeMap();
}

InputHandler::InputHandler(const InputHandler& other)
{
    m_charToScanCodeMap = other.m_charToScanCodeMap;
}

InputHandler& InputHandler::operator=(const InputHandler& other)
{
    m_charToScanCodeMap = other.m_charToScanCodeMap;
    return *this;
}

InputHandler::InputHandler(InputHandler&& other)
{
    m_charToScanCodeMap = std::move(other.m_charToScanCodeMap);
}

InputHandler& InputHandler::operator=(InputHandler&& other)
{
    m_charToScanCodeMap = std::move(other.m_charToScanCodeMap);
    return *this;
}


bool InputHandler::IsKeyPressed(const char val)
{
    //Should be threadsafe as its only doing reads
    auto sdlKeyState = SDL_GetKeyboardState(nullptr);

    if(m_charToScanCodeMap.find(val) == m_charToScanCodeMap.end())
    {
        //Key not found 
        return false;
    }
    return sdlKeyState[m_charToScanCodeMap.at(val)];
    
}

} //namespace engine