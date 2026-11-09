#include "engine/physics.hpp"
#include "engine/entity.hpp"
#include <SDL3/SDL_rect.h>


namespace engine {

namespace {

SDL_FRect boundsOf(const Transform& transform, const Collider& collider) {
    const glm::vec2 at = transform.position + collider.offset * transform.scale;
    const glm::vec2 size = collider.size * transform.scale;
    return {at.x, at.y, size.x, size.y};
}

} // namespace

PhysicsSystem::PhysicsSystem(float gravity) : gravity_(gravity) {}

void PhysicsSystem::setGravity(float gravity) noexcept {
    gravity_ = gravity;
}

float PhysicsSystem::gravity() const noexcept {
    return gravity_;
}

void PhysicsSystem::step(Scene& scene, float deltaSeconds) const {
    for (auto& [id, rigidBody] : scene.rigidBodies()) {
        rigidBody.velocity.y += gravity_ * deltaSeconds;

        Transform& transform = scene.transform(id);
        transform.position += rigidBody.velocity * deltaSeconds;
    }
}

bool PhysicsSystem::isCollision(Scene& scene, EntityId entityID1, EntityId entityID2) const
{
    auto e1Pos = scene.transform(entityID1);
    auto e2Pos = scene.transform(entityID2);
    auto e1Collider = scene.getCollider(entityID1);
    auto e2Collider = scene.getCollider(entityID2);

    if (!e1Collider || !e2Collider){
        //One or more entities is missing a collider 
        return false;
    }

    const SDL_FRect e1 = boundsOf(e1Pos, *e1Collider);
    const SDL_FRect e2 = boundsOf(e2Pos, *e2Collider);

    bool retVal = SDL_HasRectIntersectionFloat(&e1, &e2);
    return retVal;
}

Rect PhysicsSystem::GetCollisionOverlap(Scene& scene, EntityId entityID1, EntityId entityID2) const
{
    auto e1Pos = scene.transform(entityID1);
    auto e2Pos = scene.transform(entityID2);
    auto e1Collider = scene.getCollider(entityID1);
    auto e2Collider = scene.getCollider(entityID2);

    if (!e1Collider || !e2Collider){
        //One or more entities is missing a collider 
        return Rect{{0,0}, {0,0}};
    }

    const SDL_FRect e1 = boundsOf(e1Pos, *e1Collider);
    const SDL_FRect e2 = boundsOf(e2Pos, *e2Collider);
    SDL_FRect e3 = {0.0, 0.0, 0.0, 0.0};

    if (!SDL_GetRectIntersectionFloat(&e1, &e2, &e3)) {
        return Rect{{0, 0}, {0, 0}};
    }

    return Rect{{e3.x, e3.y}, {e3.w, e3.h}};
}

} // namespace engine
