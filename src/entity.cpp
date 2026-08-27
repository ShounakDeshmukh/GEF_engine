#include "engine/entity.hpp"

#include <stdexcept>
#include <unordered_map>

namespace engine {

namespace {

template <typename T>
T& addComponent(std::unordered_map<EntityId, T>& store, EntityId id, T value, bool idExists,
                const char* what) {
    if (!idExists) {
        throw std::out_of_range(what);
    }
    return store.insert_or_assign(id, value).first->second;
}

template <typename T>
T* getComponent(std::unordered_map<EntityId, T>& store, EntityId id) noexcept {
    auto it = store.find(id);
    return it != store.end() ? &it->second : nullptr;
}

template <typename T>
const T* getComponent(const std::unordered_map<EntityId, T>& store, EntityId id) noexcept {
    auto it = store.find(id);
    return it != store.end() ? &it->second : nullptr;
}

} // namespace

EntityId World::createEntity() {
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
    return addComponent(rigidBodies_, id, rb, exists(id), "World::addRigidBody: unknown EntityId");
}

void World::removeRigidBody(EntityId id) noexcept {
    rigidBodies_.erase(id);
}

RigidBody* World::getRigidBody(EntityId id) noexcept {
    return getComponent(rigidBodies_, id);
}

const RigidBody* World::getRigidBody(EntityId id) const noexcept {
    return getComponent(rigidBodies_, id);
}

Collider& World::addCollider(EntityId id, Collider c) {
    return addComponent(colliders_, id, c, exists(id), "World::addCollider: unknown EntityId");
}

void World::removeCollider(EntityId id) noexcept {
    colliders_.erase(id);
}

Collider* World::getCollider(EntityId id) noexcept {
    return getComponent(colliders_, id);
}

const Collider* World::getCollider(EntityId id) const noexcept {
    return getComponent(colliders_, id);
}

Shape& World::addShape(EntityId id, Shape s) {
    return addComponent(shapes_, id, s, exists(id), "World::addShape: unknown EntityId");
}

void World::removeShape(EntityId id) noexcept {
    shapes_.erase(id);
}

Shape* World::getShape(EntityId id) noexcept {
    return getComponent(shapes_, id);
}

const Shape* World::getShape(EntityId id) const noexcept {
    return getComponent(shapes_, id);
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
