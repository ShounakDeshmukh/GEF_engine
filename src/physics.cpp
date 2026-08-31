#include "engine/physics.hpp"
#include "engine/entity.hpp"
#include <SDL3/SDL_rect.h>


namespace engine {

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

bool PhysicsSystem::isCollision(Scene& scene, int entityID1, int entityID2) const
{
    auto e1Pos = scene.transform(entityID1);
    auto e2Pos = scene.transform(entityID2);
    auto e1Collider = scene.getCollider(entityID1);
    auto e2Collider = scene.getCollider(entityID2);

    SDL_FRect e1 = {e1Pos.position.x, e1Pos.position.y, e1Collider->size.x, e1Collider->size.y};
    SDL_FRect e2 = {e2Pos.position.x, e2Pos.position.y, e2Collider->size.x, e2Collider->size.y};
    SDL_FRect e3 = {0.0, 0.0, 0.0, 0.0};

    bool retVal = SDL_GetRectIntersectionFloat(&e1, &e2, &e3);
    return retVal;
}

} // namespace engine
