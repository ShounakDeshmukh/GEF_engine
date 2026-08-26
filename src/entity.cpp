#include "engine/entity.hpp"

#include <stdexcept>

namespace engine {

EntityId World::create() {
    const EntityId id = nextId_++;
    transforms_.try_emplace(id);
    return id;
}

void World::destroy(EntityId id) noexcept {
    transforms_.erase(id);
    rigidBodies_.erase(id);
    colliders_.erase(id);
    shapes_.erase(id);
}

bool World::exists(EntityId id) const noexcept {
    return transforms_.contains(id);
}

Transform& World::transform(EntityId id) {
    auto it = transforms_.find(id);
    if (it == transforms_.end()) {
        throw std::out_of_range("World::transform: unknown EntityId");
    }
    return it->second;
}

const Transform& World::transform(EntityId id) const {
    auto it = transforms_.find(id);
    if (it == transforms_.end()) {
        throw std::out_of_range("World::transform: unknown EntityId");
    }
    return it->second;
}

RigidBody& World::addRigidBody(EntityId id, RigidBody rb) {
    if (!exists(id)) {
        throw std::out_of_range("World::addRigidBody: unknown EntityId");
    }
    return rigidBodies_.insert_or_assign(id, rb).first->second;
}

void World::removeRigidBody(EntityId id) noexcept {
    rigidBodies_.erase(id);
}

RigidBody* World::rigidBody(EntityId id) noexcept {
    auto it = rigidBodies_.find(id);
    return it != rigidBodies_.end() ? &it->second : nullptr;
}

const RigidBody* World::rigidBody(EntityId id) const noexcept {
    auto it = rigidBodies_.find(id);
    return it != rigidBodies_.end() ? &it->second : nullptr;
}

Collider& World::addCollider(EntityId id, Collider c) {
    if (!exists(id)) {
        throw std::out_of_range("World::addCollider: unknown EntityId");
    }
    return colliders_.insert_or_assign(id, c).first->second;
}

void World::removeCollider(EntityId id) noexcept {
    colliders_.erase(id);
}

Collider* World::collider(EntityId id) noexcept {
    auto it = colliders_.find(id);
    return it != colliders_.end() ? &it->second : nullptr;
}

const Collider* World::collider(EntityId id) const noexcept {
    auto it = colliders_.find(id);
    return it != colliders_.end() ? &it->second : nullptr;
}

Shape& World::addShape(EntityId id, Shape s) {
    if (!exists(id)) {
        throw std::out_of_range("World::addShape: unknown EntityId");
    }
    return shapes_.insert_or_assign(id, s).first->second;
}

void World::removeShape(EntityId id) noexcept {
    shapes_.erase(id);
}

Shape* World::shape(EntityId id) noexcept {
    auto it = shapes_.find(id);
    return it != shapes_.end() ? &it->second : nullptr;
}

const Shape* World::shape(EntityId id) const noexcept {
    auto it = shapes_.find(id);
    return it != shapes_.end() ? &it->second : nullptr;
}

const std::unordered_map<EntityId, Transform>& World::transforms() const noexcept {
    return transforms_;
}

std::unordered_map<EntityId, RigidBody>& World::rigidBodies() noexcept {
    return rigidBodies_;
}

const std::unordered_map<EntityId, Collider>& World::colliders() const noexcept {
    return colliders_;
}

const std::unordered_map<EntityId, Shape>& World::shapes() const noexcept {
    return shapes_;
}

} // namespace engine
