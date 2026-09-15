#pragma once

#include "engine/inputScancodes.hpp"

#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>

namespace engine {
class InputHandler {

public:
    InputHandler();
    ~InputHandler();

    void updateInputState();

    void startInputHandlingThread(int updateRateInMilliseconds);
    void closeInputHandlingThread();

    bool isKeyPressed(SC::SDL_Scancode scancode);

private:
    bool* m_keyboardState;

    std::mutex m_mutex;
    std::thread m_updateThread;
    std::atomic<bool> m_isUpdateThreadRunning {false};
    int m_updateRate {0};

};

} // namespace engine
