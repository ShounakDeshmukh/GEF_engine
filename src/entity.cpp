#include "engine/entity.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace engine {

EntityId Scene::createEntity() {
    const EntityId id = nextId_;
    transforms_.try_emplace(id); // throws first if the entity pool is full
    ++nextId_;
    return id;
}

void Scene::destroyEntity(EntityId id) noexcept {
    transforms_.erase(id);
    for (auto& [type, store] : stores_) {
        store.erase(store.map, id);
    }
}

bool Scene::hasEntity(EntityId id) const noexcept {
    return transforms_.contains(id);
}

void Scene::reserveEntities(std::size_t capacity) {
    if (!transforms_.empty()) {
        throw std::logic_error("Scene::reserveEntities: the Scene already has entities");
    }
    transforms_ = componentTable<Transform>(capacity);
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
    return add<RigidBody>(id, std::move(rb));
}

void Scene::removeRigidBody(EntityId id) noexcept {
    remove<RigidBody>(id);
}

RigidBody* Scene::getRigidBody(EntityId id) noexcept {
    return get<RigidBody>(id);
}

const RigidBody* Scene::getRigidBody(EntityId id) const noexcept {
    return get<RigidBody>(id);
}

Collider& Scene::addCollider(EntityId id, Collider c) {
    return add<Collider>(id, std::move(c));
}

void Scene::removeCollider(EntityId id) noexcept {
    remove<Collider>(id);
}

Collider* Scene::getCollider(EntityId id) noexcept {
    return get<Collider>(id);
}

const Collider* Scene::getCollider(EntityId id) const noexcept {
    return get<Collider>(id);
}

Shape& Scene::addShape(EntityId id, Shape s) {
    return add<Shape>(id, std::move(s));
}

void Scene::removeShape(EntityId id) noexcept {
    remove<Shape>(id);
}

Shape* Scene::getShape(EntityId id) noexcept {
    return get<Shape>(id);
}

const Shape* Scene::getShape(EntityId id) const noexcept {
    return get<Shape>(id);
}

SpriteAnimation& Scene::addSpriteAnimation(EntityId id, SpriteAnimation anim) {
    return add<SpriteAnimation>(id, std::move(anim));
}

void Scene::removeSpriteAnimation(EntityId id) noexcept {
    remove<SpriteAnimation>(id);
}

SpriteAnimation* Scene::getSpriteAnimation(EntityId id) noexcept {
    return get<SpriteAnimation>(id);
}

const SpriteAnimation* Scene::getSpriteAnimation(EntityId id) const noexcept {
    return get<SpriteAnimation>(id);
}

Text& Scene::addText(EntityId id, Text text) {
    return add<Text>(id, std::move(text));
}

void Scene::removeText(EntityId id) noexcept {
    remove<Text>(id);
}

Text* Scene::getText(EntityId id) noexcept {
    return get<Text>(id);
}

const Text* Scene::getText(EntityId id) const noexcept {
    return get<Text>(id);
}

const componentTable<Transform>& Scene::transforms() const noexcept {
    return transforms_;
}

componentTable<RigidBody>& Scene::rigidBodies() noexcept {
    return all<RigidBody>();
}

const componentTable<Collider>& Scene::colliders() const noexcept {
    return all<Collider>();
}

const componentTable<Shape>& Scene::shapes() const noexcept {
    return all<Shape>();
}

componentTable<SpriteAnimation>& Scene::spriteAnimations() noexcept {
    return all<SpriteAnimation>();
}

const componentTable<Text>& Scene::texts() const noexcept {
    return all<Text>();
}

} // namespace engine
