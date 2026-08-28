#include "engine/physics.hpp"

namespace engine {

PhysicsSystem::PhysicsSystem(float gravity) : gravity_(gravity) {}

void PhysicsSystem::setGravity(float gravity) noexcept {
    gravity_ = gravity;
}

float PhysicsSystem::gravity() const noexcept {
    return gravity_;
}
} // namespace engine
