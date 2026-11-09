#pragma once

/** @file
 *  sceneReplicator snapshot layout, shared with snapshotDelta. Internal to the engine.
 *
 *  snapshot  u8 kFormatVersion, u32 count, count x record
 *  record    NetId, u8 mask, Transform, then RigidBody / Collider / Shape / extra as
 *            flagged in mask
 */

#include <cstdint>

#include "engine/entity.hpp"
#include "engine/ids.hpp"
#include "byteCodec.hpp"

namespace engine::networking {

    inline constexpr std::uint8_t kFormatVersion = 2;

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
        std::uint8_t shapeFlipX = 0;
        Bytes extra;
    };

    inline bool readRecord(byteReader& in, entityRecord& r)
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
            in.get(r.shapeFlipX);
        }
        if(r.mask & kHasExtra) {in.getBytes(r.extra);}
        return in.ok();
    }

}
