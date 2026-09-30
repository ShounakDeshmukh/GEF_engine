#pragma once

#include "bytes.hpp"
#include "engine/log.hpp"
#include "networkShared.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

namespace engine::networking {
class connectionManager;

/** One thread owns the reply socket. stop() may be called from another thread. */
class responseHandler {
public:
    explicit responseHandler(std::string endpoint);
    ~responseHandler();
    void run();
    void run(std::function<std::string(std::string)> func);
    /** keepRunning is checked between requests and on receive timeouts. */
    template <typename T, typename U, typename Func>
    void run(Func&& func, std::function<bool()> keepRunning = {}) {
        if (running_.exchange(true)) {
            log::error("responseHandler already running");
            return;
        }
        struct Guard {
            std::atomic<bool>& running;
            ~Guard() { running = false; }
        } guard{running_};
        stop_ = false;
        while (!stop_ && (!keepRunning || keepRunning())) {
            U request{};
            const auto status = receive(request);
            if (status == ReceivedStatus::NoMessage)
                continue;
            T reply{};
            auto error = NetworkError::None;
            if (status == ReceivedStatus::InvalidSize) {
                error = NetworkError::InvalidRequestSize;
            } else {
                try {
                    reply = std::invoke(func, request);
                } catch (...) {
                    error = NetworkError::HandlerError;
                }
            }
            send(reply, error);
        }
    }
    void stop() noexcept { stop_ = true; }
    /** Configure before run(); finite timeouts allow stop() to finish without new traffic. */
    void setReceiveTimeout(int milliseconds);
    int port() const noexcept { return port_; }

private:
    std::unique_ptr<connectionManager> connection_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{true};
    int port_ = -1;
    std::string endpoint_;
    ReceivedStatus receiveRaw(void* data, std::size_t size);
    ReceivedStatus receiveRaw(Bytes& data);
    void sendRaw(const void* data, std::size_t size, NetworkError error);
    template <typename T> ReceivedStatus receive(T& data) {
        static_assert(std::is_trivially_copyable_v<T>);
        return receiveRaw(&data, sizeof(T));
    }
    template <typename T> void send(const T& data, NetworkError error) {
        static_assert(std::is_trivially_copyable_v<T>);
        sendRaw(&data, sizeof(T), error);
    }
    ReceivedStatus receive(Bytes& data) { return receiveRaw(data); }
    void send(const Bytes& data, NetworkError error) { sendRaw(data.data(), data.size(), error); }
};
} // namespace engine::networking
