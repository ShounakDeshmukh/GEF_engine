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

    SDL_FRect e1 = {e1Pos.position.x, e1Pos.position.y, e1Collider->size.x, e1Collider->size.y};
    SDL_FRect e2 = {e2Pos.position.x, e2Pos.position.y, e2Collider->size.x, e2Collider->size.y};

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

    SDL_FRect e1 = {e1Pos.position.x, e1Pos.position.y, e1Collider->size.x, e1Collider->size.y};
    SDL_FRect e2 = {e2Pos.position.x, e2Pos.position.y, e2Collider->size.x, e2Collider->size.y};
    SDL_FRect e3 = {0.0, 0.0, 0.0, 0.0};

    SDL_GetRectIntersectionFloat(&e1, &e2, &e3);
    
    auto retVal = Rect{{e3.x, e3.y}, {e3.w, e3.h}};
    return retVal;
}

} // namespace engine
