#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "engine/ids.hpp"
#include "bytes.hpp"

namespace engine { class Scene; }

namespace engine::networking {

    /** Converts replicated entities in a Scene to and from snapshot Bytes, keyed by NetId.
     *
     *  Each snapshot carries, per entity: Transform, and RigidBody, Collider and Shape
     *  when present. Shape.texture, Text and SpriteAnimation are not sent: TextureId and
     *  FontId are process-local handles. Games send what they need to rebuild them
     *  (e.g. a texture key) through setExtra(). An existing local Shape keeps its texture
     *  when a snapshot updates it.
     *
     *  Ownership: the owner of a NetId (ownerOf(id)) is authoritative for it. apply()
     *  never overwrites entities this process owns.
     *
     *  Snapshots use the host's byte order; all processes must share an architecture.
     *
     *  @thread_safety Not threadsafe. Use from the thread that owns the Scene. */
    class sceneReplicator {
        public:
        explicit sceneReplicator(ClientId self);

        ClientId self() const {return self_;}

        // ---- sender side ----------------------------------------------------------

        /** Replicates a local entity this process owns. Allocates makeNetId(self, n).
         *  Returns the existing NetId if already tracked. */
        NetId track(EntityId entity);
        /** Stops replicating; receivers destroy their copy on the next snapshot. */
        void untrack(EntityId entity);

        /** Game-defined bytes sent with the entity on every snapshot until changed. */
        void setExtra(NetId id, Bytes extra);

        /** Entities this process owns. Tracked entities no longer in scene are skipped. */
        Bytes encodeOwned(const Scene& scene) const;
        /** Owned entities plus every remote entity currently applied here, for a server
         *  that relays clients' entities to each other. */
        Bytes encodeAll(const Scene& scene) const;

        // ---- receiver side --------------------------------------------------------

        /** Attaches a pre-built local entity (e.g. level geometry both sides create) to
         *  an agreed NetId, so the first snapshot updates it instead of spawning a
         *  duplicate. On the NetId's owner this tracks the entity under that NetId. */
        void bind(NetId id, EntityId entity);

        /** Spawns, updates and destroys entities. Entities previously received from
         *  `from` but missing from this snapshot are destroyed. Returns false, changing
         *  nothing, if the snapshot is malformed. */
        bool apply(Scene& scene, ByteView snapshot, ClientId from);

        /** Destroys every entity owned by owner (e.g. on RosterChange::Left). */
        void dropOwner(Scene& scene, ClientId owner);

        std::optional<EntityId> entityOf(NetId id) const;
        std::optional<NetId> netIdOf(EntityId entity) const;
        /** Last extra bytes set or received for id, or nullptr. */
        const Bytes* extraOf(NetId id) const;

        private:
        ClientId self_;
        std::uint32_t nextLocal_ = 0;
        std::unordered_map<NetId, EntityId> entities_;
        std::unordered_map<EntityId, NetId> netIds_;
        std::unordered_map<NetId, Bytes> extras_;
        std::unordered_map<ClientId, std::unordered_set<NetId>> receivedFrom_;   // for despawn

        Bytes encode(const Scene& scene, bool ownedOnly) const;
        void forget(Scene& scene, NetId id);
    };

}
