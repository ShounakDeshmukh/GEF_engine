#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "session.hpp"
#include "sceneReplicator.hpp"

namespace engine { class Scene; }

namespace engine::networking {

    /** Client side of a client-server session.
     *
     *  Owns two threads once started: an update loop (request/reply to this client's
     *  own server thread) and a snapshot listener (subscriber). Neither follows gameTime,
     *  so pausing the game keeps the heartbeat going; the update rate follows the game
     *  because submit() is called from onTick.
     *
     *  @thread_safety Bytes-level calls are threadsafe. Scene-level calls and replicator()
     *  touch the Scene and must come from the thread that owns it (the sim thread).
     *  Construct, join(), start() and leave() from the owning thread. */
    class sessionClient {
        public:
        /** connectionString is the server's join endpoint, e.g. "tcp://localhost:5555".
         *  hello is forwarded to every client's roster. */
        sessionClient(std::string connectionString, Bytes hello = {});
        ~sessionClient();                           // leave()

        /** Blocking handshake, one attempt of at most the reply timeout. Safe to call
         *  again after a failure; retries reuse a join token so the server hands back
         *  the same ClientId rather than consuming a new one. */
        bool join();
        /** kServerId until join() succeeds. */
        ClientId id() const;

        /** After join(). Starts the update loop and snapshot listener. */
        void start();
        /** Tells the server we left, then stops the threads. Idempotent. */
        void leave();

        /** Before start(). */
        void setReplyTimeout(int milliseconds);
        /** Before start(). An empty update is sent after this long without a submit()
         *  or send(), so a paused client is not timed out. */
        void setHeartbeat(int milliseconds);

        /** False before start(), after leave(), and once the server has not replied for
         *  kDefaultClientTimeoutMs or has dropped this client. */
        bool connected() const;

        // ---- state ----------------------------------------------------------------

        /** threadsafe, non-blocking. One update is in flight at a time; a newer submit
         *  replaces an unsent one. */
        void submit(ByteView state, std::int64_t tick);

        /** threadsafe. Newest server snapshot not yet taken, or nullopt. */
        std::optional<receivedState> takeSnapshot();

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

        /** Sim thread only. Encodes the entities this client owns and submits them. */
        void submitScene(const Scene& scene, std::int64_t tick);

        /** Sim thread only. Applies the newest snapshot, skipping entities this client
         *  owns. Returns false if there was no new snapshot. */
        bool applySnapshot(Scene& scene);

        private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

}
