#pragma once

#include "engine/animation.hpp"
#include "engine/componentTable.hpp"
#include "engine/renderer.hpp"
#include "engine/renderTypes.hpp"

#include <any>
#include <cstddef>
#include <cstdint>
#include <string>
#include <glm/vec2.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace engine {


/** Position and scale of an entity. position is the top-left corner,
 *  matching SDL_FRect; there is no center-origin conversion. */
struct Transform {
    glm::vec2 position{0.f, 0.f};
    glm::vec2 scale{1.f, 1.f};
};

/** Linear velocity for a physics-driven entity. */
struct RigidBody {
    glm::vec2 velocity{0.f, 0.f};
};

/** Axis-aligned collision extents. The box starts offset from the Transform's position, so
 *  it can fit the opaque part of a sprite with transparent padding. */
struct Collider {
    glm::vec2 size{0.f, 0.f};
    glm::vec2 offset{0.f, 0.f};
};

/** Visual size, color, and optional texture used when rendering an entity. */
struct Shape {
    glm::vec2 size{32.f, 32.f};
    Color color;
    std::optional<TextureId> texture; // nullopt = solid color; present = textured
    bool tiled = false;               // true = repeat texture across size instead of stretching
    bool flipX = false;               // true = mirror the texture or sprite frame left to right
};

struct Text {
    std::string val;
    FontId font;
    Color color{255, 255, 255, 255};
};

/** Owns every entity and its components. Every entity has a Transform; any other
 *  copy-constructible type is a component (add<T>, get<T>, each<T...>), and the named
 *  accessors (addShape, getCollider, ...) are shorthands for the same storage.
 *
 *  Each component type, and the entities' Transforms, live in a componentTable. By default
 *  a table allocates its entries on the heap; reservePool<T>(n) and reserveEntities(n) switch
 *  it to a fixed pool with memory for n reserved up front. A copied Scene gets its own pools
 *  of the same capacities.
 *
 *  @thread_safety Not thread-safe. */
class Scene {
public:
    /** Creates a new entity with a default Transform and no other
     *  components. Returns its id. Throws std::length_error if reserveEntities() set a
     *  limit and that many entities are live. */
    EntityId createEntity();
    /** Destroys an entity and all of its components. A no-op if id does
     *  not refer to a live entity. */
    void destroyEntity(EntityId id) noexcept;
    /** True if id refers to a live entity. */
    bool hasEntity(EntityId id) const noexcept;

    /** Pools the entities' Transforms: reserves memory for capacity entities now, and
     *  createEntity() throws std::length_error once that many are live. Throws
     *  std::logic_error if the Scene already has entities. */
    void reserveEntities(std::size_t capacity);

    /** Pools component T: reserves memory for capacity Ts now, and add<T> throws
     *  std::length_error once that many are attached. Call it before the first add<T>;
     *  throws std::logic_error if any entity already has a T. */
    template <typename T> void reservePool(std::size_t capacity) {
        componentMap<T>& store = ensureStore<T>();
        if (!store.empty()) {
            throw std::logic_error("Scene::reservePool: the component type already has entries");
        }
        store = componentMap<T>(capacity);
    }

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

    /** Attaches text to id, or overwrites existing text */
    Text& addText(EntityId id, Text text);
    /** Detaches id's Text component, if exists */
    void removeText(EntityId id) noexcept;
    /** id's Text Component, nullptr if doesn't exist */
    Text* getText(EntityId id) noexcept;
    /** id's Text Component, nullptr if doesn't exist */
    const Text* getText(EntityId id) const noexcept;


    /** All live entities' transforms. */
    const componentTable<Transform>& transforms() const noexcept;
    /** All RigidBodies, mutable. */
    componentTable<RigidBody>& rigidBodies() noexcept;
    /** All Colliders. */
    const componentTable<Collider>& colliders() const noexcept;
    /** All Shapes. */
    const componentTable<Shape>& shapes() const noexcept;
    /** All SpriteAnimations, mutable. */
    componentTable<SpriteAnimation>& spriteAnimations() noexcept;
    /** All Text Components */
    const componentTable<Text>& texts() const noexcept;

    /** Attaches (or overwrites) a T on id. Throws std::out_of_range if id does not
     *  refer to a live entity, or std::length_error if T is pooled and the pool is full. */
    template <typename T> T& add(EntityId id, T value = {}) {
        if (!hasEntity(id)) {
            throw std::out_of_range("Scene::add: unknown EntityId");
        }
        return ensureStore<T>().insert_or_assign(id, std::move(value)).first->second;
    }

    /** Detaches id's T, if any. */
    template <typename T> void remove(EntityId id) noexcept {
        if (auto* store = storeOf<T>()) {
            store->erase(id);
        }
    }

    /** id's T, or nullptr if it has none. */
    template <typename T> T* get(EntityId id) noexcept {
        return const_cast<T*>(std::as_const(*this).get<T>(id));
    }
    template <typename T> const T* get(EntityId id) const noexcept {
        const auto* store = storeOf<T>();
        if (!store) {
            return nullptr;
        }
        auto it = store->find(id);
        return it != store->end() ? &it->second : nullptr;
    }

    template <typename T> bool has(EntityId id) const noexcept { return get<T>(id) != nullptr; }

    /** Every T, keyed by entity. capacity() and pooled() on the result describe T's pool. */
    template <typename T> componentTable<T>& all() { return ensureStore<T>(); }
    template <typename T> const componentTable<T>& all() const {
        static const componentTable<T> empty;
        const auto* store = storeOf<T>();
        return store ? *store : empty;
    }

    /** Calls f(id, T0&, Ts&...) for every entity that has all of them. Iterates T0's
     *  store, so put the rarest type first; f must not add or remove a T0. */
    template <typename T0, typename... Ts, typename F> void each(F&& f) {
        for (auto& [id, first] : ensureStore<T0>()) {
            if ((has<Ts>(id) && ...)) {
                f(id, first, *get<Ts>(id)...);
            }
        }
    }

private:
    template <typename T> using componentMap = componentTable<T>;

    /** One component type's map, type-erased; std::any keeps Scene copyable. */
    struct componentStore {
        std::any map;
        void (*erase)(std::any& map, EntityId id);
    };

    template <typename T> const componentMap<T>* storeOf() const noexcept {
        static_assert(std::is_copy_constructible_v<T>,
                      "Scene components must be copy-constructible: RenderFrame copies the Scene");
        static_assert(!std::is_same_v<T, Transform>,
                      "Transform is built in: use Scene::transform(id)");
        auto it = stores_.find(typeid(T));
        return it != stores_.end() ? std::any_cast<componentMap<T>>(&it->second.map) : nullptr;
    }
    template <typename T> componentMap<T>* storeOf() noexcept {
        return const_cast<componentMap<T>*>(std::as_const(*this).storeOf<T>());
    }

    template <typename T> componentMap<T>& ensureStore() {
        if (auto* store = storeOf<T>()) {
            return *store;
        }
        auto erase = [](std::any& map, EntityId id) {
            std::any_cast<componentMap<T>&>(map).erase(id);
        };
        auto [it, inserted] = stores_.emplace(typeid(T), componentStore{componentMap<T>{}, erase});
        return std::any_cast<componentMap<T>&>(it->second.map);
    }

    EntityId nextId_ = 1;
    componentTable<Transform> transforms_;
    std::unordered_map<std::type_index, componentStore> stores_;
};

} // namespace engine
