#include "engine/networking/sceneReplicator.hpp"
#include "engine/entity.hpp"
#include "byteCodec.hpp"

#include <cassert>
#include <nlohmann/json.hpp>
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

        Bytes writeBinary(const std::vector<entityRecord>& records)
        {
            Bytes out;
            byteWriter w(out);
            w.put(kFormatVersion);
            w.put(static_cast<std::uint32_t>(records.size()));
            for(const auto& r : records)
            {
                w.put(r.id);
                w.put(r.mask);
                w.put(r.transform);
                if(r.mask & kHasRigidBody) {w.put(r.rigidBody);}
                if(r.mask & kHasCollider) {w.put(r.collider);}
                if(r.mask & kHasShape)
                {
                    w.put(r.shapeSize);
                    w.put(r.shapeColor);
                    w.put(r.shapeTiled);
                }
                if(r.mask & kHasExtra) {w.putBytes(r.extra);}
            }
            return out;
        }

        bool readBinary(ByteView snapshot, std::vector<entityRecord>& records)
        {
            byteReader in(snapshot);
            std::uint8_t version = 0;
            std::uint32_t count = 0;
            in.get(version);
            in.get(count);
            if(!in.ok() || version != kFormatVersion) {return false;}

            for(std::uint32_t i = 0; i < count; ++i)
            {
                entityRecord r;
                if(!readRecord(in, r)) {return false;}
                records.push_back(std::move(r));
            }
            return in.atEnd();
        }

        nlohmann::json toJson(glm::vec2 v) {return nlohmann::json::array({v.x, v.y});}

        glm::vec2 vec2From(const nlohmann::json& j)
        {
            return {j.at(0).get<float>(), j.at(1).get<float>()};
        }

        Bytes writeJson(const std::vector<entityRecord>& records)
        {
            auto entities = nlohmann::json::array();
            for(const auto& r : records)
            {
                nlohmann::json e = {
                    {"id", r.id},
                    {"m", r.mask},
                    {"t", {{"p", toJson(r.transform.position)}, {"s", toJson(r.transform.scale)}}},
                };
                if(r.mask & kHasRigidBody) {e["rb"] = toJson(r.rigidBody.velocity);}
                if(r.mask & kHasCollider) {e["c"] = toJson(r.collider.size);}
                if(r.mask & kHasShape)
                {
                    const Color& c = r.shapeColor;
                    e["sh"] = {{"s", toJson(r.shapeSize)},
                               {"c", {c.r, c.g, c.b, c.a}},
                               {"t", r.shapeTiled != 0}};
                }
                if(r.mask & kHasExtra)
                {
                    auto& x = e["x"] = nlohmann::json::array();
                    for(std::byte b : r.extra) {x.push_back(std::to_integer<int>(b));}
                }
                entities.push_back(std::move(e));
            }
            const nlohmann::json doc = {{"v", kFormatVersion}, {"e", std::move(entities)}};
            const std::string text = doc.dump();
            const auto* begin = reinterpret_cast<const std::byte*>(text.data());
            return Bytes(begin, begin + text.size());
        }

        bool readJson(ByteView snapshot, std::vector<entityRecord>& records)
        {
            const auto* begin = reinterpret_cast<const char*>(snapshot.data());
            const auto doc = nlohmann::json::parse(begin, begin + snapshot.size(), nullptr, false);
            if(doc.is_discarded()) {return false;}

            // .at() and get<>() throw on missing keys and wrong types
            try
            {
                if(doc.at("v").get<int>() != kFormatVersion) {return false;}
                if(!doc.at("e").is_array()) {return false;}
                for(const auto& e : doc.at("e"))
                {
                    entityRecord r;
                    r.id = e.at("id").get<NetId>();
                    r.mask = e.at("m").get<std::uint8_t>();
                    r.transform.position = vec2From(e.at("t").at("p"));
                    r.transform.scale = vec2From(e.at("t").at("s"));
                    if(r.mask & kHasRigidBody) {r.rigidBody.velocity = vec2From(e.at("rb"));}
                    if(r.mask & kHasCollider) {r.collider.size = vec2From(e.at("c"));}
                    if(r.mask & kHasShape)
                    {
                        const auto& sh = e.at("sh");
                        const auto& c = sh.at("c");
                        r.shapeSize = vec2From(sh.at("s"));
                        r.shapeColor = {c.at(0).get<std::uint8_t>(), c.at(1).get<std::uint8_t>(),
                                        c.at(2).get<std::uint8_t>(), c.at(3).get<std::uint8_t>()};
                        r.shapeTiled = sh.at("t").get<bool>() ? 1 : 0;
                    }
                    if(r.mask & kHasExtra)
                    {
                        for(const auto& b : e.at("x"))
                        {
                            const int value = b.get<int>();
                            if(value < 0 || value > 255) {return false;}
                            r.extra.push_back(static_cast<std::byte>(value));
                        }
                    }
                    records.push_back(std::move(r));
                }
            }
            catch(const nlohmann::json::exception&) {return false;}
            return true;
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
        std::vector<entityRecord> records;
        for(const auto& [id, entity] : entities_)
        {
            if(ownedOnly && ownerOf(id) != self_) {continue;}
            if(!scene.hasEntity(entity)) {continue;}

            entityRecord r;
            r.id = id;
            r.transform = scene.transform(entity);
            if(const RigidBody* rb = scene.getRigidBody(entity))
            {
                r.mask |= kHasRigidBody;
                r.rigidBody = *rb;
            }
            if(const Collider* collider = scene.getCollider(entity))
            {
                r.mask |= kHasCollider;
                r.collider = *collider;
            }
            if(const Shape* shape = scene.getShape(entity))
            {
                r.mask |= kHasShape;
                r.shapeSize = shape->size;
                r.shapeColor = shape->color;
                r.shapeTiled = shape->tiled ? 1 : 0;
            }
            if(auto extra = extras_.find(id); extra != extras_.end())
            {
                r.mask |= kHasExtra;
                r.extra = extra->second;
            }
            records.push_back(std::move(r));
        }
        return format_ == SnapshotFormat::Json ? writeJson(records) : writeBinary(records);
    }

    bool sceneReplicator::apply(Scene& scene, ByteView snapshot, ClientId from)
    {
        // parse everything first so a malformed snapshot changes nothing
        std::vector<entityRecord> records;
        const bool isJson = !snapshot.empty() && snapshot.front() == std::byte{'{'};
        if(!(isJson ? readJson(snapshot, records) : readBinary(snapshot, records))) {return false;}

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
