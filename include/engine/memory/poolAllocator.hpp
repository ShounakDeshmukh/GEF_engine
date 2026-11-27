#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace engine::memory {

/** A fixed-capacity pool of T. The constructor reserves memory for capacity objects in one
 *  block; create() and destroy() then run in constant time with no further allocation.
 *
 *  Freed slots form a linked list threaded through the unused slots themselves, so the pool
 *  needs no bookkeeping memory beyond the block. Slots that have never been used are handed
 *  out in order after the free list runs dry, so constructing a pool doesn't touch the block.
 *
 *  Pointers from create() stay valid until destroy(), including across a move of the pool.
 *  Every object must be destroyed before the pool is (asserted in debug builds).
 *
 *  @thread_safety Not thread-safe. */
template <typename T> class poolAllocator {
public:
    /** Reserves room for capacity objects of T. */
    explicit poolAllocator(std::size_t capacity)
        : slots_(capacity ? new slot[capacity] : nullptr), capacity_(capacity) {}

    poolAllocator(const poolAllocator&) = delete;
    poolAllocator& operator=(const poolAllocator&) = delete;

    poolAllocator(poolAllocator&& other) noexcept
        : slots_(std::move(other.slots_)), free_(std::exchange(other.free_, nullptr)),
          capacity_(std::exchange(other.capacity_, 0)), unused_(std::exchange(other.unused_, 0)),
          live_(std::exchange(other.live_, 0)) {}

    poolAllocator& operator=(poolAllocator&& other) noexcept {
        poolAllocator moved(std::move(other));
        swap(moved);
        return *this;
    }

    ~poolAllocator() { assert(live_ == 0 && "poolAllocator destroyed with live objects"); }

    /** Constructs a T from args in a free slot and returns it. Throws std::length_error if
     *  all capacity slots are in use; if T's constructor throws, the slot stays free. */
    template <typename... Args> T* create(Args&&... args) {
        slot* s = nullptr;
        slot* next = nullptr;
        if (free_) {
            s = free_;
            next = s->next; // the object will overwrite it
        } else if (unused_ < capacity_) {
            s = &slots_[unused_];
        } else {
            throw std::length_error("poolAllocator: pool is full");
        }

        T* object = nullptr;
        try {
            object =
                std::construct_at(reinterpret_cast<T*>(s->storage), std::forward<Args>(args)...);
        } catch (...) {
            if (s == free_) {
                s->next = next; // a half-built object may have overwritten the link
            }
            throw;
        }

        if (s == free_) {
            free_ = next;
        } else {
            ++unused_;
        }
        ++live_;
        return object;
    }

    /** Destroys an object from create() and returns its slot to the pool. A no-op for
     *  nullptr. object must have come from this pool (asserted in debug builds). */
    void destroy(T* object) noexcept {
        if (!object) {
            return;
        }
        assert(owns(object) && "poolAllocator::destroy: object is not from this pool");
        std::destroy_at(object);
        slot& s = slots_[indexOf(object)];
        s.next = free_;
        free_ = &s;
        --live_;
    }

    /** True if p points at a slot of this pool that has been handed out at least once. */
    bool owns(const T* p) const noexcept {
        if (!p || !slots_) {
            return false;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(p);
        const auto base = reinterpret_cast<std::uintptr_t>(slots_.get());
        if (address < base) {
            return false;
        }
        const std::uintptr_t offset = address - base;
        return offset % sizeof(slot) == 0 && offset / sizeof(slot) < unused_;
    }

    /** The number of objects the pool can hold. */
    std::size_t capacity() const noexcept { return capacity_; }
    /** The number of live objects. */
    std::size_t size() const noexcept { return live_; }
    /** True if create() would throw. */
    bool full() const noexcept { return live_ == capacity_; }

    void swap(poolAllocator& other) noexcept {
        using std::swap;
        swap(slots_, other.slots_);
        swap(free_, other.free_);
        swap(capacity_, other.capacity_);
        swap(unused_, other.unused_);
        swap(live_, other.live_);
    }

private:
    /** One object's worth of storage; holds the free-list link while the slot is free. */
    union slot {
        slot* next;
        alignas(T) std::byte storage[sizeof(T)];
    };

    std::size_t indexOf(const T* p) const noexcept {
        return (reinterpret_cast<std::uintptr_t>(p) -
                reinterpret_cast<std::uintptr_t>(slots_.get())) /
               sizeof(slot);
    }

    std::unique_ptr<slot[]> slots_;
    slot* free_ = nullptr; // most recently freed slot, or nullptr
    std::size_t capacity_ = 0;
    std::size_t unused_ = 0; // slots [unused_, capacity_) have never been handed out
    std::size_t live_ = 0;
};

} // namespace engine::memory
