#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "session.hpp"
#include "sceneReplicator.hpp"

namespace engine { class Scene; }

namespace engine::networking {

    /** Client side of a client-server session.
     *
     *  Owns one thread once started: an update loop (request/reply to this client's
     *  own server thread). Snapshots arrive in the replies. It does not follow gameTime,
     *  so pausing the game keeps the heartbeat going; the update rate follows the game
     *  because submitScene() is called from onTick.
     *
     *  @thread_safety Message and roster calls are threadsafe. Scene calls and
     *  replicator() touch the Scene and must come from the thread that owns it (the sim
     *  thread).
     *  Construct, join(), start() and leave() from the owning thread. */
    class sessionClient {
        public:
        /** connectionString is the server's join endpoint, e.g. "tcp://localhost:5555".
         *  hello and peerEndpoint are forwarded to every client's roster; peerEndpoint is
         *  where other clients reach this one directly (see peerSession). */
        sessionClient(std::string connectionString, Bytes hello = {}, std::string peerEndpoint = "");
        ~sessionClient();                           // leave()

        /** Blocking handshake, one attempt of at most the reply timeout. Safe to call
         *  again after a failure; retries reuse a join token so the server hands back
         *  the same ClientId rather than consuming a new one. */
        bool join();
        /** kServerId until join() succeeds. */
        ClientId id() const;

        /** After join(). Starts the update loop. */
        void start();
        /** Tells the server we left, then stops the update thread. Idempotent. */
        void leave();

        /** How long each join and update waits for the server's reply. Defaults to
         *  requestHandler::kDefaultReplyTimeoutMs; takes effect from the next request. */
        void setReplyTimeout(int milliseconds);
        /** An empty update is sent after this long without a submitScene() or send(),
         *  so a paused client is not timed out. Defaults to kDefaultHeartbeatMs; takes
         *  effect from the next wait. */
        void setHeartbeat(int milliseconds);

        /** False before start(), after leave(), and once the server has not replied for
         *  kDefaultClientTimeoutMs or has dropped this client. */
        bool connected() const;

        // ---- messages -------------------------------------------------------------

        /** threadsafe. Sent with the next update and resent until acknowledged. */
        void send(std::uint16_t type, Bytes payload, std::int64_t tick);

        /** threadsafe. All server messages since the last drain, in order. */
        std::deque<receivedMessage> drainMessages();

        // ---- roster ---------------------------------------------------------------

        /** threadsafe. The first drain after start reports everyone already connected,
         *  this client included, as Joined. */
        std::deque<rosterEvent> drainRosterEvents();

        // ---- scene ----------------------------------------------------------------

        /** Sim thread only; valid after join(). track() this client's own entities
         *  (its player) here. */
        sceneReplicator& replicator();

        /** Sim thread only. Encodes the entities this client owns and submits them.
         *  Non-blocking: one update is in flight at a time, and a newer submit replaces
         *  an unsent one. */
        void submitScene(const Scene& scene, std::int64_t tick);

        /** Sim thread only. Applies the newest snapshot, skipping entities this client
         *  owns. Returns false if there was no new snapshot. */
        bool applySnapshot(Scene& scene);

        private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

}
