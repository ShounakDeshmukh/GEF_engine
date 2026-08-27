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

EntityId Scene::createEntity() {
    const EntityId id = nextId_++;
    transforms_.try_emplace(id);
    return id;
}

void Scene::destroyEntity(EntityId id) noexcept {
    transforms_.erase(id);
    rigidBodies_.erase(id);
    colliders_.erase(id);
    shapes_.erase(id);
}

bool Scene::hasEntity(EntityId id) const noexcept {
    return transforms_.contains(id);
}

Transform& Scene::transform(EntityId id) {
    auto it = transforms_.find(id);
    if (it == transforms_.end()) {
        throw std::out_of_range("Scene::transform: unknown EntityId");
    }
    return it->second;
}

const Transform& Scene::transform(EntityId id) const {
    auto it = transforms_.find(id);
    if (it == transforms_.end()) {
        throw std::out_of_range("Scene::transform: unknown EntityId");
    }
    return it->second;
}

RigidBody& Scene::addRigidBody(EntityId id, RigidBody rb) {
    return addComponent(rigidBodies_, id, rb, hasEntity(id),
                        "Scene::addRigidBody: unknown EntityId");
}

void Scene::removeRigidBody(EntityId id) noexcept {
    rigidBodies_.erase(id);
}

RigidBody* Scene::getRigidBody(EntityId id) noexcept {
    return getComponent(rigidBodies_, id);
}

const RigidBody* Scene::getRigidBody(EntityId id) const noexcept {
    return getComponent(rigidBodies_, id);
}

Collider& Scene::addCollider(EntityId id, Collider c) {
    return addComponent(colliders_, id, c, hasEntity(id), "Scene::addCollider: unknown EntityId");
}

void Scene::removeCollider(EntityId id) noexcept {
    colliders_.erase(id);
}

Collider* Scene::getCollider(EntityId id) noexcept {
    return getComponent(colliders_, id);
}

const Collider* Scene::getCollider(EntityId id) const noexcept {
    return getComponent(colliders_, id);
}

Shape& Scene::addShape(EntityId id, Shape s) {
    return addComponent(shapes_, id, s, hasEntity(id), "Scene::addShape: unknown EntityId");
}

void Scene::removeShape(EntityId id) noexcept {
    shapes_.erase(id);
}

Shape* Scene::getShape(EntityId id) noexcept {
    return getComponent(shapes_, id);
}

const Shape* Scene::getShape(EntityId id) const noexcept {
    return getComponent(shapes_, id);
}

const std::unordered_map<EntityId, Transform>& Scene::transforms() const noexcept {
    return transforms_;
}

std::unordered_map<EntityId, RigidBody>& Scene::rigidBodies() noexcept {
    return rigidBodies_;
}

const std::unordered_map<EntityId, Collider>& Scene::colliders() const noexcept {
    return colliders_;
}

const std::unordered_map<EntityId, Shape>& Scene::shapes() const noexcept {
    return shapes_;
}

} // namespace engine
