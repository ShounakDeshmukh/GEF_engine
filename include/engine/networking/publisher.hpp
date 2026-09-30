#pragma once

#include "bytes.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

namespace engine::networking {
class connectionManager;

/** Publishes topic + data messages. Calls are serialized; destruction must not overlap a call. */
class publisher {
public:
    explicit publisher(std::string connectionString);
    ~publisher();
    void publish(std::string input, std::string topic = "");
    void publish(ByteView data, const std::string& topic = "");
    void publish(const Bytes& data, const std::string& topic = "") {
        publish(ByteView{data}, topic);
    }
    template <typename T> void publish(const T& data, std::string topic = "") {
        static_assert(std::is_trivially_copyable_v<T>);
        send(&data, sizeof(T), topic);
    }
    /** Actual TCP port, including when the requested port is zero. */
    int port() const noexcept { return port_; }

private:
    std::unique_ptr<connectionManager> connection_;
    std::mutex mutex_;
    int port_ = -1;
    void send(const void* data, std::size_t size, const std::string& topic);
};
} // namespace engine::networking
