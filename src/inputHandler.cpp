#include "engine/inputHandler.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <chrono>

namespace engine {

InputHandler::InputHandler()
{
    m_keyboardState = new bool[SDL_SCANCODE_COUNT];
}
    
InputHandler::~InputHandler()
{
    closeInputHandlingThread();
     delete[] m_keyboardState;
}


void InputHandler::startInputHandlingThread(int updateRateInMilliseconds)
{
    m_updateRate = updateRateInMilliseconds;
    if (!m_isUpdateThreadRunning)
    {
        m_isUpdateThreadRunning = true;
        auto inputLambda = [this](){
            while(m_isUpdateThreadRunning)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_updateRate));
                updateInputState();
            }
        };
    
        m_updateThread = std::thread(inputLambda);
    }
}

void InputHandler::closeInputHandlingThread()
{
    if(m_isUpdateThreadRunning)
    {
        m_isUpdateThreadRunning = false;
        m_updateThread.join();
    }
}



bool InputHandler::isKeyPressed(SC::SDL_Scancode scancode) {

    auto scancodeVal = static_cast<int>(scancode);
    if (scancodeVal < 0 || scancodeVal >= SDL_SCANCODE_COUNT)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard_(m_mutex);
    return m_keyboardState[scancode];
}

void InputHandler::updateInputState()
{
    std::lock_guard<std::mutex> guard_(m_mutex);
    int keyboardsize = 0;
    auto sdlKeyState = SDL_GetKeyboardState(&keyboardsize);
    
    memcpy(m_keyboardState, sdlKeyState, SDL_SCANCODE_COUNT);
}

} // namespace engine
