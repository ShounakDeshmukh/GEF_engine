#pragma once

#include "engine/memory/poolAllocator.hpp"

#include <cassert>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

namespace engine::memory {

/** Creates and destroys objects of T either on the heap or from a poolAllocator, chosen when
 *  the allocator is constructed. Code that holds an objectAllocator<T> works the same way in
 *  both modes, so a game can switch a type to a pool without touching the code that uses it.
 *
 *  - objectAllocator<T>() - heap mode: create() is new, destroy() is delete; no limit.
 *  - objectAllocator<T>(n) - pool mode: memory for n objects is reserved now, and create()
 *    throws std::length_error once n are live.
 *
 *  Every object must be destroyed through the allocator that created it, before that
 *  allocator is destroyed (asserted in debug builds).
 *
 *  @thread_safety Not thread-safe. */
template <typename T> class objectAllocator {
public:
    /** Heap mode. */
    objectAllocator() noexcept = default;
    /** Pool mode with room for capacity objects, reserved now. */
    explicit objectAllocator(std::size_t capacity) : pool_(std::in_place, capacity) {}

    objectAllocator(const objectAllocator&) = delete;
    objectAllocator& operator=(const objectAllocator&) = delete;

    /** Takes other's objects and mode; other is left an empty heap-mode allocator. */
    objectAllocator(objectAllocator&& other) noexcept
        : pool_(std::exchange(other.pool_, std::nullopt)),
          heapLive_(std::exchange(other.heapLive_, 0)) {}

    objectAllocator& operator=(objectAllocator&& other) noexcept {
        objectAllocator moved(std::move(other));
        swap(moved);
        return *this;
    }

    ~objectAllocator() { assert(heapLive_ == 0 && "objectAllocator destroyed with live objects"); }

    /** Constructs a T from args and returns it. In pool mode, throws std::length_error if
     *  the pool is full. */
    template <typename... Args> T* create(Args&&... args) {
        if (pool_) {
            return pool_->create(std::forward<Args>(args)...);
        }
        T* object = new T(std::forward<Args>(args)...);
        ++heapLive_;
        return object;
    }

    /** Destroys an object from create(). A no-op for nullptr. */
    void destroy(T* object) noexcept {
        if (!object) {
            return;
        }
        if (pool_) {
            pool_->destroy(object);
        } else {
            delete object;
            --heapLive_;
        }
    }

    /** True in pool mode. */
    bool pooled() const noexcept { return pool_.has_value(); }
    /** The pool's capacity, or SIZE_MAX in heap mode. */
    std::size_t capacity() const noexcept {
        return pool_ ? pool_->capacity() : std::numeric_limits<std::size_t>::max();
    }
    /** The number of live objects. */
    std::size_t size() const noexcept { return pool_ ? pool_->size() : heapLive_; }
    /** True if create() would throw; never in heap mode. */
    bool full() const noexcept { return pool_ && pool_->full(); }

    void swap(objectAllocator& other) noexcept {
        pool_.swap(other.pool_);
        std::swap(heapLive_, other.heapLive_);
    }

private:
    std::optional<poolAllocator<T>> pool_; // empty = heap mode
    std::size_t heapLive_ = 0;
};

} // namespace engine::memory
