#include "engine/physics.hpp"
#include "engine/entity.hpp"

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
} // namespace engine
