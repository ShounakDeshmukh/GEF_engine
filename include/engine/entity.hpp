#pragma once

#include "engine/renderer.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <optional>
#include <unordered_map>

namespace engine {

/** Opaque handle to an entity. Values are assigned by World::create() and are
 *  never reused for the lifetime of a World. */
using EntityId = std::uint32_t;

/** Position and scale of an entity. position is the top-left corner,
 *  matching SDL_FRect; there is no center-origin conversion. */
struct Transform {
    glm::vec2 position{0.f, 0.f};
    glm::vec2 scale{1.f, 1.f};
};

/** Linear velocity and optional gravity for a physics-driven entity. */
struct RigidBody {
    glm::vec2 velocity{0.f, 0.f};
    std::optional<float> gravity; // nullopt = unaffected; present = gravity
                                   // accel for this entity (px/s^2)
};

/** Axis-aligned collision extents. */
struct Collider {
    glm::vec2 size{0.f, 0.f};
};

/** Visual size and color used when rendering an entity. */
struct Shape {
    glm::vec2 size{32.f, 32.f};
    Color color;
};

/** Owns every entity and its components. All access goes through a method
 *  rather than a public member.
 *
 *  @thread_safety Not thread-safe. */
class World {
public:
    /** Creates a new entity with a default Transform and no other
     *  components. Returns its id. */
    EntityId create();
    /** Destroys an entity and all of its components. A no-op if id does
     *  not refer to a live entity. */
    void destroy(EntityId id) noexcept;
    /** True if id refers to a live entity. */
    bool exists(EntityId id) const noexcept;

    /** The entity's transform. Throws std::out_of_range if id does not
     *  refer to a live entity. */
    Transform& transform(EntityId id);
    /** The entity's transform. Throws std::out_of_range if id does not
     *  refer to a live entity. */
    const Transform& transform(EntityId id) const;

    /** Attaches (or overwrites) a RigidBody on id. Throws std::out_of_range
     *  if id does not refer to a live entity. */
    RigidBody& addRigidBody(EntityId id, RigidBody rb = {});
    /** Detaches id's RigidBody, if any. A no-op if it has none. */
    void removeRigidBody(EntityId id) noexcept;
    /** id's RigidBody, or nullptr if it has none. */
    RigidBody* rigidBody(EntityId id) noexcept;
    /** id's RigidBody, or nullptr if it has none. */
    const RigidBody* rigidBody(EntityId id) const noexcept;

    /** Attaches (or overwrites) a Collider on id. Throws std::out_of_range
     *  if id does not refer to a live entity. */
    Collider& addCollider(EntityId id, Collider c);
    /** Detaches id's Collider, if any. A no-op if it has none. */
    void removeCollider(EntityId id) noexcept;
    /** id's Collider, or nullptr if it has none. */
    Collider* collider(EntityId id) noexcept;
    /** id's Collider, or nullptr if it has none. */
    const Collider* collider(EntityId id) const noexcept;

    /** Attaches (or overwrites) a Shape on id. Throws std::out_of_range if
     *  id does not refer to a live entity. */
    Shape& addShape(EntityId id, Shape s);
    /** Detaches id's Shape, if any. A no-op if it has none. */
    void removeShape(EntityId id) noexcept;
    /** id's Shape, or nullptr if it has none. */
    Shape* shape(EntityId id) noexcept;
    /** id's Shape, or nullptr if it has none. */
    const Shape* shape(EntityId id) const noexcept;

    /** All live entities' transforms. */
    const std::unordered_map<EntityId, Transform>& transforms() const noexcept;
    /** All RigidBodies, mutable. */
    std::unordered_map<EntityId, RigidBody>& rigidBodies() noexcept;
    /** All Colliders. */
    const std::unordered_map<EntityId, Collider>& colliders() const noexcept;
    /** All Shapes. */
    const std::unordered_map<EntityId, Shape>& shapes() const noexcept;

private:
    EntityId nextId_ = 1;
    std::unordered_map<EntityId, Transform> transforms_;
    std::unordered_map<EntityId, RigidBody> rigidBodies_;
    std::unordered_map<EntityId, Collider> colliders_;
    std::unordered_map<EntityId, Shape> shapes_;
};

} // namespace engine
