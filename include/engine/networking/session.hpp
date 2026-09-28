#pragma once

#include "engine/networking/protocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::networking {

/** A game-thread event. Network threads never access Scene or SDL objects. */
struct PeerEvent {
    enum class Kind { Welcome, World, Player } kind;
    ClientId sender = 0;
    PlayerPosition position{};
    WorldSnapshot world{};
};

/** Owns registration, the peer directory, and shared world state. Player positions bypass it. */
class CoordinatorServer {
public:
    using WorldPosition = std::function<PlayerPosition(double elapsedSeconds)>;
    CoordinatorServer(std::uint16_t registrationPort, std::uint16_t firstControlPort,
                      WorldPosition worldPosition);
    ~CoordinatorServer();
    CoordinatorServer(const CoordinatorServer&) = delete;
    CoordinatorServer& operator=(const CoordinatorServer&) = delete;
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Exchanges player positions directly with peers and queues received events for the game thread. */
class PeerClient {
public:
    /** advertisedEndpoint must be reachable by other game processes. */
    PeerClient(std::string serverEndpoint, std::string advertisedEndpoint,
               int updatesPerSecond = 20);
    ~PeerClient();
    PeerClient(const PeerClient&) = delete;
    PeerClient& operator=(const PeerClient&) = delete;
    void start();
    void stop();
    void publish(PlayerPosition position); // Replaces the pending local position snapshot.
    std::vector<PeerEvent> drain(); // Called by the game thread to apply network changes.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::networking
