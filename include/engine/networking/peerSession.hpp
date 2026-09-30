#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/ids.hpp"
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
     *  @thread_safety Not threadsafe. Construct, join(), start(), leave() and the Scene
     *  calls from the owning (sim) thread. */
    class peerSession {
        public:
        /** Binds this peer's publisher on an ephemeral port immediately; throws if that
         *  fails. advertisedHost is how other peers reach it: the default suits peers on
         *  one machine, use this machine's address on a LAN. */
        peerSession(std::string serverConnectionString, std::string advertisedHost = "localhost");
        ~peerSession();                             // leave()

        /** Joins the server, advertising this peer's publisher. See sessionClient::join(). */
        bool join();
        /** kServerId until join() succeeds. */
        ClientId id() const;

        /** After join(). Starts the server connection; peers connect from its roster. */
        void start();
        /** Disconnects every peer, then leaves the server. Idempotent. */
        void leave();

        /** Publishes the entities this peer owns to every connected peer, and asks the
         *  server for its newest snapshot. Call from onTick so both rates follow the
         *  game's speed. */
        void publishScene(const Scene& scene, std::int64_t tick);

        /** Connects and disconnects peers from roster changes, applies the server's
         *  newest snapshot and each peer's newest state, and destroys the entities of
         *  peers that left. */
        void applyUpdates(Scene& scene);

        /** Valid after join(). track() this peer's own entities (its player) here. */
        sceneReplicator& replicator();

        /** Peers currently connected, excluding this one. */
        std::vector<ClientId> peers() const;

        private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

}
