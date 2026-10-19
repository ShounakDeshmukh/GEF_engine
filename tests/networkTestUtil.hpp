#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace testutil {

/** Polls pred every 5 ms until it is true or timeout elapses. */
inline bool eventually(const std::function<bool()>& pred,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

/** Endpoint for reaching a socket this process bound on port 0. */
inline std::string localEndpoint(int port) {
    return "tcp://127.0.0.1:" + std::to_string(port);
}

} // namespace testutil
