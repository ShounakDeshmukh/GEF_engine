#include "engine/networking/sceneReplicator.hpp"
#include "engine/entity.hpp"
#include "byteCodec.hpp"

#include <cassert>
#include <vector>

namespace engine::networking {

    namespace {
        constexpr std::uint8_t kFormatVersion = 1;

        enum : std::uint8_t {
            kHasRigidBody = 1 << 0,
            kHasCollider  = 1 << 1,
            kHasShape     = 1 << 2,
            kHasExtra     = 1 << 3,
        };

        struct entityRecord {
            NetId id = 0;
            std::uint8_t mask = 0;
            Transform transform;
            RigidBody rigidBody;
            Collider collider;
            glm::vec2 shapeSize{0.f, 0.f};
            Color shapeColor;
            std::uint8_t shapeTiled = 0;
            Bytes extra;
        };

        bool readRecord(byteReader& in, entityRecord& r)
        {
            in.get(r.id);
            in.get(r.mask);
            in.get(r.transform);
            if(r.mask & kHasRigidBody) {in.get(r.rigidBody);}
            if(r.mask & kHasCollider) {in.get(r.collider);}
            if(r.mask & kHasShape)
            {
                in.get(r.shapeSize);
                in.get(r.shapeColor);
                in.get(r.shapeTiled);
            }
            if(r.mask & kHasExtra) {in.getBytes(r.extra);}
            return in.ok();
        }
    }

    sceneReplicator::sceneReplicator(ClientId self): self_(self) {}

    NetId sceneReplicator::track(EntityId entity)
    {
        if(auto it = netIds_.find(entity); it != netIds_.end()) {return it->second;}

        assert(nextLocal_ <= kNetIdLocalMask && "NetId space exhausted for this owner");
        NetId id = makeNetId(self_, nextLocal_++);
        entities_[id] = entity;
        netIds_[entity] = id;
        return id;
    }

    void sceneReplicator::untrack(EntityId entity)
    {
        auto it = netIds_.find(entity);
        if(it == netIds_.end()) {return;}
        entities_.erase(it->second);
        extras_.erase(it->second);
        netIds_.erase(it);
    }

    void sceneReplicator::setExtra(NetId id, Bytes extra)
    {
        extras_[id] = std::move(extra);
    }

    void sceneReplicator::bind(NetId id, EntityId entity)
    {
        if(auto old = entities_.find(id); old != entities_.end()) {netIds_.erase(old->second);}
        if(auto old = netIds_.find(entity); old != netIds_.end()) {entities_.erase(old->second);}
        entities_[id] = entity;
        netIds_[entity] = id;

        // keep track() from handing out an id bound here
        if(ownerOf(id) == self_ && localOf(id) >= nextLocal_) {nextLocal_ = localOf(id) + 1;}
    }

    Bytes sceneReplicator::encodeOwned(const Scene& scene) const
    {
        return encode(scene, true);
    }

    Bytes sceneReplicator::encodeAll(const Scene& scene) const
    {
        return encode(scene, false);
    }

    Bytes sceneReplicator::encode(const Scene& scene, bool ownedOnly) const
    {
        Bytes body;
        byteWriter w(body);
        std::uint32_t count = 0;

        for(const auto& [id, entity] : entities_)
        {
            if(ownedOnly && ownerOf(id) != self_) {continue;}
            if(!scene.hasEntity(entity)) {continue;}

            const RigidBody* rb = scene.getRigidBody(entity);
            const Collider* collider = scene.getCollider(entity);
            const Shape* shape = scene.getShape(entity);
            auto extra = extras_.find(id);

            std::uint8_t mask = 0;
            if(rb) {mask |= kHasRigidBody;}
            if(collider) {mask |= kHasCollider;}
            if(shape) {mask |= kHasShape;}
            if(extra != extras_.end()) {mask |= kHasExtra;}

            w.put(id);
            w.put(mask);
            w.put(scene.transform(entity));
            if(rb) {w.put(*rb);}
            if(collider) {w.put(*collider);}
            if(shape)
            {
                w.put(shape->size);
                w.put(shape->color);
                w.put(static_cast<std::uint8_t>(shape->tiled));
            }
            if(extra != extras_.end()) {w.putBytes(extra->second);}
            ++count;
        }

        Bytes out;
        byteWriter header(out);
        header.put(kFormatVersion);
        header.put(count);
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    bool sceneReplicator::apply(Scene& scene, ByteView snapshot, ClientId from)
    {
        // parse everything first so a malformed snapshot changes nothing
        byteReader in(snapshot);
        std::uint8_t version = 0;
        std::uint32_t count = 0;
        in.get(version);
        in.get(count);
        if(!in.ok() || version != kFormatVersion) {return false;}

        std::vector<entityRecord> records;
        for(std::uint32_t i = 0; i < count; ++i)
        {
            entityRecord r;
            if(!readRecord(in, r)) {return false;}
            records.push_back(std::move(r));
        }
        if(!in.atEnd()) {return false;}

        std::unordered_set<NetId> seen;
        for(auto& r : records)
        {
            if(ownerOf(r.id) == self_) {continue;}
            seen.insert(r.id);

            EntityId entity;
            auto it = entities_.find(r.id);
            if(it != entities_.end() && scene.hasEntity(it->second))
            {
                entity = it->second;
            }
            else
            {
                if(it != entities_.end()) {netIds_.erase(it->second);}
                entity = scene.createEntity();
                entities_[r.id] = entity;
                netIds_[entity] = r.id;
            }

            scene.transform(entity) = r.transform;

            if(r.mask & kHasRigidBody) {scene.addRigidBody(entity, r.rigidBody);}
            else {scene.removeRigidBody(entity);}

            if(r.mask & kHasCollider) {scene.addCollider(entity, r.collider);}
            else {scene.removeCollider(entity);}

            if(r.mask & kHasShape)
            {
                Shape shape;
                shape.size = r.shapeSize;
                shape.color = r.shapeColor;
                shape.tiled = r.shapeTiled != 0;
                if(const Shape* local = scene.getShape(entity)) {shape.texture = local->texture;}
                scene.addShape(entity, shape);
            }
            else {scene.removeShape(entity);}

            if(r.mask & kHasExtra) {extras_[r.id] = std::move(r.extra);}
            else {extras_.erase(r.id);}
        }

        auto& previous = receivedFrom_[from];
        for(NetId id : previous)
        {
            if(!seen.contains(id)) {forget(scene, id);}
        }
        previous = std::move(seen);
        return true;
    }

    void sceneReplicator::dropOwner(Scene& scene, ClientId owner)
    {
        if(owner == self_) {return;}

        std::vector<NetId> owned;
        for(const auto& [id, entity] : entities_)
        {
            if(ownerOf(id) == owner) {owned.push_back(id);}
        }
        for(NetId id : owned)
        {
            forget(scene, id);
            for(auto& [source, ids] : receivedFrom_) {ids.erase(id);}
        }
        receivedFrom_.erase(owner);
    }

    void sceneReplicator::forget(Scene& scene, NetId id)
    {
        auto it = entities_.find(id);
        if(it != entities_.end())
        {
            scene.destroyEntity(it->second);
            netIds_.erase(it->second);
            entities_.erase(it);
        }
        extras_.erase(id);
    }

    std::optional<EntityId> sceneReplicator::entityOf(NetId id) const
    {
        auto it = entities_.find(id);
        if(it == entities_.end()) {return std::nullopt;}
        return it->second;
    }

    std::optional<NetId> sceneReplicator::netIdOf(EntityId entity) const
    {
        auto it = netIds_.find(entity);
        if(it == netIds_.end()) {return std::nullopt;}
        return it->second;
    }

    const Bytes* sceneReplicator::extraOf(NetId id) const
    {
        auto it = extras_.find(id);
        return it == extras_.end() ? nullptr : &it->second;
    }

}
