#pragma once
#include "protocol.hpp"

#include <chrono>
#include <memory>

namespace engine::networking {
struct ClientConfig {
    std::string serverHost = "localhost";
    std::uint16_t joinPort = 5555;
    std::chrono::milliseconds replyTimeout{1000};
    std::chrono::milliseconds heartbeat{500};
    bool peerToPeer = false;
    std::string advertisedHost = "127.0.0.1";
    std::uint16_t peerPort = 0;         // 0 selects an available port.
    std::size_t outboundCapacity = 256; // Oldest unsent states are dropped when full.
};
struct ServerEvent {
    enum class Kind { World, Player, PlayerLeft } kind;
    ClientId client = 0;
    std::int64_t tick = 0;
    Bytes payload;
};
/** Same game API in both modes. Set peerToPeer to the inverse of ServerConfig::relayPlayers.
 *  join() starts the network workers and returns the assigned ID (nullopt on failure).
 *  start() is a convenience that throws on a failed join. stop() sends Leave and joins workers.
 *  send()/drain() are thread-safe. Serialize lifecycle calls; create a new Client after stop().
 *  A lost server session requires a new Client/join; individual request timeouts recover.
 *  Network threads own sockets; only game code interprets payloads or touches a Scene. */
class Client {
public:
    explicit Client(ClientConfig config = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    std::optional<ClientId> join();
    void start();
    void stop();
    ClientId id() const noexcept;
    /** Call once per onTick with this tick's state, even if unchanged. No periodic resend.
     *  While paused, stop calling send(); real-time heartbeats keep the session connected. */
    void send(Bytes playerState, std::int64_t tick);
    /** Keeps latest world/player states until drained; a departure supersedes pending state.
     *  To display remote updates during local pause, drain independently of onTick and apply
     *  them with SimulationThread::post(). Only the simulation thread should change its Scene. */
    std::vector<ServerEvent> drain();
    std::uint64_t droppedUpdates() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace engine::networking
