#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "engine/ids.hpp"
#include "session.hpp"
#include "sceneReplicator.hpp"

namespace engine { class Scene; }

namespace engine::networking {

    /** Hybrid peer: the server provides the shared scene (e.g. moving platforms) and the
     *  roster; each peer's own entities (e.g. its player) go directly to every other peer.
     *
     *  Built on a sessionClient for the server side, one publisher for this peer's state,
     *  and one subscriber thread per connected peer. Peers are connected and disconnected
     *  from the server's roster.
     *
     *  Do not also send this peer's entities to the server (sessionClient::submitScene):
     *  the same NetId arriving from two sources makes it flicker between them.
     *
     *  @thread_safety send(), drainMessages() and connected() are threadsafe. Everything
     *  else, including construction, join(), start(), leave() and the Scene calls, runs on
     *  the owning (sim) thread. */
    class peerSession {
        public:
        /** Binds this peer's publisher on an ephemeral port immediately; throws if that
         *  fails. advertisedHost is how other peers reach it: the default suits peers on
         *  one machine, use this machine's address on a LAN. hello goes to every client's
         *  roster, as in sessionClient. */
        peerSession(std::string serverConnectionString, std::string advertisedHost = "localhost",
                    Bytes hello = {});
        ~peerSession();                             // leave()

        /** Joins the server, advertising this peer's publisher. See sessionClient::join(). */
        bool join();
        /** kServerId until join() succeeds. */
        ClientId id() const;

        /** After join(). Starts the server connection; peers connect from its roster. */
        void start();
        /** Disconnects every peer, then leaves the server. Idempotent. */
        void leave();

        /** False before start(), after leave(), and once the server has not replied for
         *  kDefaultClientTimeoutMs or has dropped this peer. Peers already connected keep
         *  exchanging state; shared server objects stop updating. */
        bool connected() const;

        /** Publishes the entities this peer owns to every connected peer, and asks the
         *  server for its newest snapshot. Call from onTick so both rates follow the
         *  game's speed. */
        void publishScene(const Scene& scene, std::int64_t tick);

        /** Connects and disconnects peers from roster changes, applies the server's
         *  newest snapshot and each peer's newest state, and destroys the entities of
         *  peers that left. */
        void applyUpdates(Scene& scene);

        // ---- messages (to and from the server; see sessionClient) ---------------------
        void send(std::uint16_t type, Bytes payload, std::int64_t tick);
        std::deque<receivedMessage> drainMessages();

        // ---- roster -------------------------------------------------------------------
        /** The roster as of the last applyUpdates(), this peer included. */
        std::vector<clientInfo> roster() const;

        /** Valid after join(). track() this peer's own entities (its player) here. */
        sceneReplicator& replicator();

        /** Peers currently connected, excluding this one. */
        std::vector<ClientId> peers() const;

        private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

}
