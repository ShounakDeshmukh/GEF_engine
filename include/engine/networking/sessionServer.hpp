#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "session.hpp"
#include "sceneReplicator.hpp"

namespace engine { class Scene; }

namespace engine::networking {

    /** Headless-server side of a client-server session.
     *
     *  Clients join on connectionString. Each accepted client gets a ClientId (1..255,
     *  never reused within a run) and its own thread and reply socket for updates. Each
     *  client's snapshot is sent in the reply to that client's own update, on that
     *  client's thread.
     *
     *  Two kinds of traffic:
     *  - Scene state (applyClientStates / publishScene): latest wins, older values may
     *    be skipped.
     *  - Messages (send / broadcast): every one delivered, in order, per client.
     *
     *  @thread_safety Message and roster calls are threadsafe. Scene calls and
     *  replicator() touch the Scene and must come from the thread that owns it (the sim
     *  thread).
     *  Construct, start() and stop() from the owning thread. */
    class sessionServer {
        public:
        /** Binds immediately; throws if the endpoint is unavailable. */
        sessionServer(std::string connectionString = "tcp://*:5555");
        ~sessionServer();                           // stop()

        /** Port clients join on; useful when binding port 0 for an ephemeral port. */
        int port() const;

        /** Starts accepting clients. Call once. */
        void start();
        /** Idempotent. Joins all network threads. */
        void stop();

        /** A client silent this long is removed (RosterChange::Left). Defaults to
         *  kDefaultClientTimeoutMs; takes effect immediately. */
        void setClientTimeout(int milliseconds);

        // ---- messages -------------------------------------------------------------

        /** threadsafe. Queued until the client's next update, then resent until acknowledged.
         *  Dropped if `to` is not connected, including after it leaves. */
        void send(ClientId to, std::uint16_t type, Bytes payload, std::int64_t tick);
        /** threadsafe. send() to every connected client. */
        void broadcast(std::uint16_t type, Bytes payload, std::int64_t tick);

        /** threadsafe. All messages from all clients since the last drain, in order per client. */
        std::deque<receivedMessage> drainMessages();

        // ---- roster ---------------------------------------------------------------

        /** threadsafe. Joins, leaves and timeouts since the last drain. */
        std::deque<rosterEvent> drainRosterEvents();
        std::vector<clientInfo> roster() const;

        // ---- scene ----------------------------------------------------------------

        /** Sim thread only. track() or bind() server-owned entities (e.g. platforms) here. */
        sceneReplicator& replicator();

        /** Sim thread only. Encodes every replicated entity (server-owned and those
         *  received from clients) as the current snapshot; each client's thread sends it
         *  in that client's next reply. Clients keep only the newest. */
        void publishScene(const Scene& scene, std::int64_t tick);

        /** Sim thread only. Applies each client's newest submitted scene state and
         *  destroys entities owned by clients that left. */
        void applyClientStates(Scene& scene);

        private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

}
