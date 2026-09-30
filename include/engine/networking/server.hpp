#pragma once
#include "protocol.hpp"

#include <chrono>
#include <memory>

namespace engine::networking {
struct ServerConfig {
    std::string bindAddress = "tcp://*";
    std::uint16_t joinPort = 5555; // 0 selects an available port.
    std::chrono::milliseconds clientTimeout{3000};
    bool relayPlayers = true;
};
struct ClientEvent {
    enum class Kind { Joined, Left, Update } kind;
    ClientId client;
    std::int64_t tick = 0;
    Bytes payload;
};
/** Shared-world authority and directory for either relay or peer mode.
 *  Uses one reply thread per client. Network threads never access Scene, SDL or Timeline.
 *  publishWorld()/drain() are thread-safe; serialize start()/stop()/destruction on the owner.
 *  A server run assigns at most 255 IDs, without reuse. */
class Server {
public:
    explicit Server(ServerConfig config = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start();
    void stop();
    std::uint16_t port() const noexcept;
    /** Call from the server's onTick, after stepping the shared world.
     *  The complete snapshot (world + players + directory) must fit in 64 KiB, including
     *  Welcome overhead. Throws length_error if this update would exceed that budget. */
    void publishWorld(Bytes world, std::int64_t tick);
    /** Drain regularly; the event queue retains at most 4096 events, dropping the oldest. */
    std::vector<ClientEvent> drain();
    std::size_t connectedClients() const;
    std::size_t relayedPlayerCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine::networking
