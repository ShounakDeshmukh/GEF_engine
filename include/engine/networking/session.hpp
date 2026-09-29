#pragma once

/** @file
 *  Coordinator and peer sessions for a game that exchanges player positions
 *  directly between clients. Network workers communicate with the game thread
 *  through position snapshots and queued events.
 */

#include "engine/networking/protocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::networking {

/** An event to apply on the game thread. Network threads never access Scene or
 *  SDL objects. Only the fields associated with kind contain event data.
 */
struct PeerEvent {
    /** Welcome assigns this client's ID in sender; World fills world; Player
     *  identifies the remote player in sender and fills position. */
    enum class Kind { Welcome, World, Player } kind;
    ClientId sender = 0;
    PlayerPosition position{};
    WorldSnapshot world{};
};

/** Owns registration, the peer directory, and shared world state. Player
 *  positions travel directly between PeerClient instances.
 *
 *  Each client receives a control port at firstControlPort plus its assigned
 *  ID. The coordinator serves shared-state snapshots from its control threads.
 *
 *  @thread_safety start() and stop() must be serialized with one another and
 *  with destruction. WorldPosition may be called concurrently by control
 *  threads and must not access game-thread-only state without synchronization.
 */
class CoordinatorServer {
public:
    /** Computes the shared position from seconds since coordinator construction.
     *  Called on a network control thread. */
    using WorldPosition = std::function<PlayerPosition(double elapsedSeconds)>;

    /** Configures the registration listener and per-client control ports.
     *  @param registrationPort TCP port on which clients register.
     *  @param firstControlPort Base TCP port; client ID 1 uses the next port.
     *  @param worldPosition Callback that supplies the shared position.
     */
    CoordinatorServer(std::uint16_t registrationPort, std::uint16_t firstControlPort,
                      WorldPosition worldPosition);

    /** Stops the coordinator and joins its network threads. */
    ~CoordinatorServer();
    CoordinatorServer(const CoordinatorServer&) = delete;
    CoordinatorServer& operator=(const CoordinatorServer&) = delete;

    /** Starts listening and returns once the registration socket is bound.
     *  A call while already started has no effect.
     *  @throws std::runtime_error if the ZeroMQ context or listener cannot start.
     */
    void start();

    /** Stops all listeners and joins their threads. Safe to call more than once.
     *  Construct a new coordinator for another run after stopping.
     */
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Exchanges player positions directly with peers and queues received events
 *  for the game thread. The coordinator handles registration and shared world
 *  state; it does not relay player positions.
 *
 *  Call start(), publish() current local positions, and drain() events in the
 *  game loop. Destruction stops the session. World and Player events are
 *  coalesced by kind and sender while waiting in the queue, so drain() returns
 *  the newest pending state for each.
 *
 *  @thread_safety publish() and drain() synchronize their own data and may be
 *  called from different threads. Serialize start(), stop(), and destruction
 *  with one another and with other calls. Apply drained events on the game
 *  thread; the network worker does not touch Scene or SDL objects.
 */
class PeerClient {
public:
    /** Configures a peer session.
     *  @param serverEndpoint Coordinator registration address, in
     *  tcp://host:port form.
     *  @param advertisedEndpoint This peer's tcp://host:port address. The
     *  worker binds its port and other game processes must be able to reach
     *  the advertised host and port.
     *  @param updatesPerSecond Target rate for direct position broadcasts;
     *  values below one are clamped to one.
     */
    PeerClient(std::string serverEndpoint, std::string advertisedEndpoint,
               int updatesPerSecond = 20);

    /** Stops the worker and releases its sockets. */
    ~PeerClient();
    PeerClient(const PeerClient&) = delete;
    PeerClient& operator=(const PeerClient&) = delete;

    /** Binds the peer socket, registers with the coordinator, and returns
     *  after a Welcome event is queued. A call while already started has no
     *  effect.
     *  @throws std::runtime_error if setup or registration fails or times out.
     */
    void start();

    /** Stops the worker and joins its thread. Safe to call more than once.
     *  Construct a new peer for another run after stopping.
     */
    void stop();

    /** Replaces the local position snapshot used by future broadcasts. The
     *  worker sends nothing until the first call to publish().
     */
    void publish(PlayerPosition position);

    /** Takes all pending events and clears the queue. Call from the game
     *  thread to apply network changes to game state.
     *  @return Events queued since the last drain, with intermediate World
     *  and Player updates for the same sender coalesced.
     */
    std::vector<PeerEvent> drain();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::networking
