#pragma once

#include "bytes.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace engine::networking {
class connectionManager;

/** One receiving thread owns the socket, including connect/disconnect and timeout changes. */
class subscriber {
public:
    explicit subscriber(std::string endpoint = "", std::string topic = "");
    ~subscriber();
    void connect(const std::string& endpoint);
    void disconnect(const std::string& endpoint);
    void setReceiveTimeout(int milliseconds);
    /** nullopt on timeout or malformed topic/data framing. */
    std::optional<Bytes> tryReceive();
    std::string listen();
    template <typename T> T listenT() {
        static_assert(std::is_trivially_copyable_v<T>);
        auto bytes = tryReceive();
        if (!bytes || bytes->size() != sizeof(T))
            throw std::runtime_error("Received malformed message or timed out");
        T result{};
        std::memcpy(&result, bytes->data(), sizeof(T));
        return result;
    }

private:
    std::unique_ptr<connectionManager> connection_;
    std::atomic<bool> running_{false};
};
} // namespace engine::networking
