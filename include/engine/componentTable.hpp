#pragma once

#include "engine/ids.hpp"
#include "engine/memory/objectAllocator.hpp"

#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace engine {

/** One component type's storage in a Scene: a map from EntityId to T whose entries are
 *  created through a memory::objectAllocator, so they live on the heap or in a fixed pool.
 *
 *  It reads like std::unordered_map<EntityId, T>: iterating yields std::pair<const EntityId, T>&
 *  (so `for (auto& [id, c] : table)` works), and find/contains/erase/insert_or_assign/size
 *  behave the same. Entries never move while they live, in either mode.
 *
 *  A pooled table holds at most capacity() entries; adding past that throws
 *  std::length_error. The id-to-entry index is a std::unordered_map reserved to the
 *  capacity, so its buckets don't grow, but each entry still adds one small index node.
 *
 *  Copies get their own allocator in the same mode and capacity, never a share of the
 *  source's pool.
 *
 *  @thread_safety Not thread-safe. */
template <typename T> class componentTable {
public:
    using key_type = EntityId;
    using mapped_type = T;
    using value_type = std::pair<const EntityId, T>;
    using size_type = std::size_t;

private:
    using index = std::unordered_map<EntityId, value_type*>;

    template <bool Const> class basicIterator {
        using base =
            std::conditional_t<Const, typename index::const_iterator, typename index::iterator>;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = componentTable::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const value_type&, value_type&>;
        using pointer = std::conditional_t<Const, const value_type*, value_type*>;

        basicIterator() = default;
        explicit basicIterator(base it) : it_(it) {}

        /** iterator converts to const_iterator. */
        operator basicIterator<true>() const
            requires(!Const)
        {
            return basicIterator<true>(it_);
        }

        reference operator*() const { return *it_->second; }
        pointer operator->() const { return it_->second; }
        basicIterator& operator++() {
            ++it_;
            return *this;
        }
        basicIterator operator++(int) {
            basicIterator before = *this;
            ++it_;
            return before;
        }
        friend bool operator==(const basicIterator&, const basicIterator&) = default;

    private:
        friend class componentTable;
        base it_{};
    };

public:
    using iterator = basicIterator<false>;
    using const_iterator = basicIterator<true>;

    /** Heap mode: no limit on entries. */
    componentTable() = default;

    /** Pool mode: reserves memory for capacity entries now. */
    explicit componentTable(std::size_t capacity) : allocator_(capacity) {
        index_.reserve(capacity);
    }

    componentTable(const componentTable& other)
        : allocator_(other.pooled() ? allocatorType(other.capacity()) : allocatorType()) {
        index_.reserve(other.pooled() ? other.capacity() : other.size());
        try {
            for (const auto& [id, entry] : other.index_) {
                try_emplace(id, entry->second);
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    componentTable(componentTable&& other) noexcept
        : allocator_(std::move(other.allocator_)), index_(std::move(other.index_)) {
        other.index_.clear();
    }

    componentTable& operator=(const componentTable& other) {
        if (this != &other) {
            componentTable copy(other);
            swap(copy);
        }
        return *this;
    }

    componentTable& operator=(componentTable&& other) noexcept {
        componentTable moved(std::move(other));
        swap(moved);
        return *this;
    }

    ~componentTable() { clear(); }

    iterator begin() noexcept { return iterator(index_.begin()); }
    iterator end() noexcept { return iterator(index_.end()); }
    const_iterator begin() const noexcept { return const_iterator(index_.begin()); }
    const_iterator end() const noexcept { return const_iterator(index_.end()); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    std::size_t size() const noexcept { return index_.size(); }
    bool empty() const noexcept { return index_.empty(); }

    /** True in pool mode. */
    bool pooled() const noexcept { return allocator_.pooled(); }
    /** The most entries the table can hold: the pool's capacity, or SIZE_MAX in heap mode. */
    std::size_t capacity() const noexcept { return allocator_.capacity(); }
    /** True if adding another entry would throw. */
    bool full() const noexcept { return allocator_.full(); }

    iterator find(EntityId id) noexcept { return iterator(index_.find(id)); }
    const_iterator find(EntityId id) const noexcept { return const_iterator(index_.find(id)); }
    bool contains(EntityId id) const noexcept { return index_.contains(id); }
    std::size_t count(EntityId id) const noexcept { return index_.count(id); }

    /** Adds an entry for id built from args unless id already has one. Returns the entry
     *  and whether it was added. */
    template <typename... Args> std::pair<iterator, bool> try_emplace(EntityId id, Args&&... args) {
        if (auto it = index_.find(id); it != index_.end()) {
            return {iterator(it), false};
        }
        value_type* entry = allocator_.create(std::piecewise_construct, std::forward_as_tuple(id),
                                              std::forward_as_tuple(std::forward<Args>(args)...));
        try {
            return {iterator(index_.emplace(id, entry).first), true};
        } catch (...) {
            allocator_.destroy(entry);
            throw;
        }
    }

    /** Sets id's entry to value, adding it if id has none. Returns the entry and whether it
     *  was added. */
    template <typename V> std::pair<iterator, bool> insert_or_assign(EntityId id, V&& value) {
        if (auto it = index_.find(id); it != index_.end()) {
            it->second->second = std::forward<V>(value);
            return {iterator(it), false};
        }
        return try_emplace(id, std::forward<V>(value));
    }

    /** Removes id's entry, if any. Returns the number removed (0 or 1). */
    std::size_t erase(EntityId id) noexcept {
        auto it = index_.find(id);
        if (it == index_.end()) {
            return 0;
        }
        allocator_.destroy(it->second);
        index_.erase(it);
        return 1;
    }

    /** Removes the entry at pos and returns the one after it. */
    iterator erase(const_iterator pos) noexcept {
        allocator_.destroy(pos.it_->second);
        return iterator(index_.erase(pos.it_));
    }

    /** Removes every entry. The table keeps its mode and capacity. */
    void clear() noexcept {
        for (auto& [id, entry] : index_) {
            allocator_.destroy(entry);
        }
        index_.clear();
    }

    void swap(componentTable& other) noexcept {
        allocator_.swap(other.allocator_);
        index_.swap(other.index_);
    }

private:
    using allocatorType = memory::objectAllocator<value_type>;

    allocatorType allocator_;
    index index_;
};

} // namespace engine
