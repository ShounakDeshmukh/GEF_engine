#pragma once

#include "engine/animation.hpp"
#include "engine/renderer.hpp"

#include <cstdint>
#include <glm/vec2.hpp>
#include <optional>
#include <unordered_map>

namespace engine {

/** Opaque handle to an entity. Values are assigned by Scene::createEntity() and are
 *  never reused for the lifetime of a Scene. */
using EntityId = std::uint32_t;

/** Position and scale of an entity. position is the top-left corner,
 *  matching SDL_FRect; there is no center-origin conversion. */
struct Transform {
    glm::vec2 position{0.f, 0.f};
    glm::vec2 scale{1.f, 1.f};
};

/** Linear velocity for a physics-driven entity.
 *      0.0 = unaffected by gravity
 *      1.0 = full system gravity
 *      other values multiply the system gravity
 *
 * Velocity is measured in pixels per second.
 */
struct RigidBody {
    glm::vec2 velocity{0.f, 0.f};
    float gravityScale = 0.f;
};

/** Axis-aligned collision extents. */
struct Collider {
    glm::vec2 size{0.f, 0.f};
};

/** Visual size, color, and optional texture used when rendering an entity. */
struct Shape {
    glm::vec2 size{32.f, 32.f};
    Color color;
    std::optional<TextureId> texture; // nullopt = solid color; present = textured
    bool tiled = false;               // true = repeat texture across size instead of stretching
};

/** Owns every entity and its components. All access goes through a method
 *  rather than a public member.
 *
 *  @thread_safety Not thread-safe. */
class Scene {
public:
    /** Creates a new entity with a default Transform and no other
     *  components. Returns its id. */
    EntityId createEntity();
    /** Destroys an entity and all of its components. A no-op if id does
     *  not refer to a live entity. */
    void destroyEntity(EntityId id) noexcept;
    /** True if id refers to a live entity. */
    bool hasEntity(EntityId id) const noexcept;

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
    RigidBody* getRigidBody(EntityId id) noexcept;
    /** id's RigidBody, or nullptr if it has none. */
    const RigidBody* getRigidBody(EntityId id) const noexcept;

    /** Attaches (or overwrites) a Collider on id. Throws std::out_of_range
     *  if id does not refer to a live entity. */
    Collider& addCollider(EntityId id, Collider c);
    /** Detaches id's Collider, if any. A no-op if it has none. */
    void removeCollider(EntityId id) noexcept;
    /** id's Collider, or nullptr if it has none. */
    Collider* getCollider(EntityId id) noexcept;
    /** id's Collider, or nullptr if it has none. */
    const Collider* getCollider(EntityId id) const noexcept;

    /** Attaches (or overwrites) a Shape on id. Throws std::out_of_range if
     *  id does not refer to a live entity. */
    Shape& addShape(EntityId id, Shape s);
    /** Detaches id's Shape, if any. A no-op if it has none. */
    void removeShape(EntityId id) noexcept;
    /** id's Shape, or nullptr if it has none. */
    Shape* getShape(EntityId id) noexcept;
    /** id's Shape, or nullptr if it has none. */
    const Shape* getShape(EntityId id) const noexcept;

    /** Attaches (or overwrites) a SpriteAnimation on id. Throws
     *  std::out_of_range if id does not refer to a live entity. */
    SpriteAnimation& addSpriteAnimation(EntityId id, SpriteAnimation anim);
    /** Detaches id's SpriteAnimation, if any. A no-op if it has none. */
    void removeSpriteAnimation(EntityId id) noexcept;
    /** id's SpriteAnimation, or nullptr if it has none. */
    SpriteAnimation* getSpriteAnimation(EntityId id) noexcept;
    /** id's SpriteAnimation, or nullptr if it has none. */
    const SpriteAnimation* getSpriteAnimation(EntityId id) const noexcept;

    /** All live entities' transforms. */
    const std::unordered_map<EntityId, Transform>& transforms() const noexcept;
    /** All RigidBodies, mutable. */
    std::unordered_map<EntityId, RigidBody>& rigidBodies() noexcept;
    /** All Colliders. */
    const std::unordered_map<EntityId, Collider>& colliders() const noexcept;
    /** All Shapes. */
    const std::unordered_map<EntityId, Shape>& shapes() const noexcept;
    /** All SpriteAnimations, mutable. */
    std::unordered_map<EntityId, SpriteAnimation>& spriteAnimations() noexcept;

private:
    EntityId nextId_ = 1;
    std::unordered_map<EntityId, Transform> transforms_;
    std::unordered_map<EntityId, RigidBody> rigidBodies_;
    std::unordered_map<EntityId, Collider> colliders_;
    std::unordered_map<EntityId, Shape> shapes_;
    std::unordered_map<EntityId, SpriteAnimation> spriteAnimations_;
};

} // namespace engine
