#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <engine/componentTable.hpp>
#include <engine/memory/objectAllocator.hpp>
#include <engine/memory/poolAllocator.hpp>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

/** Counts constructions and destructions, and can be told to throw from its constructor. */
struct Counted {
    static inline int alive = 0;
    static inline bool throwNext = false;

    explicit Counted(int v = 0) : value(v) {
        if (throwNext) {
            throwNext = false;
            throw std::runtime_error("Counted: asked to throw");
        }
        ++alive;
    }
    Counted(const Counted& other) : value(other.value) { ++alive; }
    Counted& operator=(const Counted&) = default;
    ~Counted() { --alive; }

    int value;
};

struct alignas(64) OverAligned {
    char byte = 0;
};

} // namespace

TEST_CASE("poolAllocator hands out distinct slots up to its capacity", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(3);
    REQUIRE(pool.capacity() == 3);
    REQUIRE(pool.size() == 0);

    Counted* a = pool.create(1);
    Counted* b = pool.create(2);
    Counted* c = pool.create(3);

    REQUIRE(std::set<Counted*>{a, b, c}.size() == 3);
    REQUIRE(pool.owns(a));
    REQUIRE(pool.owns(b));
    REQUIRE(pool.owns(c));
    REQUIRE(a->value == 1);
    REQUIRE(c->value == 3);
    REQUIRE(pool.size() == 3);
    REQUIRE(pool.full());
    REQUIRE(Counted::alive == 3);
    REQUIRE_THROWS_AS(pool.create(4), std::length_error);

    pool.destroy(a);
    pool.destroy(b);
    pool.destroy(c);
    REQUIRE(Counted::alive == 0);
    REQUIRE(pool.size() == 0);
}

TEST_CASE("poolAllocator reuses a destroyed slot", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(2);
    Counted* a = pool.create(1);
    Counted* b = pool.create(2);

    pool.destroy(a);
    REQUIRE_FALSE(pool.full());
    Counted* again = pool.create(3);

    REQUIRE(again == a);
    REQUIRE(again->value == 3);

    pool.destroy(again);
    pool.destroy(b);
}

TEST_CASE("poolAllocator::owns rejects pointers from elsewhere", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(2);
    Counted outside(0);
    Counted* inPool = pool.create(1);

    REQUIRE(pool.owns(inPool));
    REQUIRE_FALSE(pool.owns(&outside));
    REQUIRE_FALSE(pool.owns(nullptr));

    pool.destroy(inPool);
}

TEST_CASE("poolAllocator respects over-aligned types", "[memory][pool]") {
    engine::memory::poolAllocator<OverAligned> pool(4);
    std::vector<OverAligned*> objects;
    for (int i = 0; i < 4; ++i) {
        objects.push_back(pool.create());
        REQUIRE(reinterpret_cast<std::uintptr_t>(objects.back()) % 64 == 0);
    }
    for (OverAligned* o : objects) {
        pool.destroy(o);
    }
}

TEST_CASE("poolAllocator keeps a slot free when the constructor throws", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(2);

    SECTION("from a never-used slot") {
        Counted::throwNext = true;
        REQUIRE_THROWS_AS(pool.create(1), std::runtime_error);
        REQUIRE(pool.size() == 0);
    }
    SECTION("from a recycled slot") {
        pool.destroy(pool.create(1));
        Counted::throwNext = true;
        REQUIRE_THROWS_AS(pool.create(2), std::runtime_error);
        REQUIRE(pool.size() == 0);
    }

    // Both slots must still be usable, and the free list must not be corrupted.
    Counted* a = pool.create(3);
    Counted* b = pool.create(4);
    REQUIRE(a != b);
    REQUIRE_THROWS_AS(pool.create(5), std::length_error);
    pool.destroy(a);
    pool.destroy(b);
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("poolAllocator objects stay put when the pool moves", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(2);
    Counted* a = pool.create(7);

    engine::memory::poolAllocator<Counted> moved(std::move(pool));

    REQUIRE(moved.owns(a));
    REQUIRE(a->value == 7);
    REQUIRE(moved.size() == 1);
    REQUIRE(pool.capacity() == 0);
    moved.destroy(a);
}

TEST_CASE("objectAllocator in heap mode has no limit", "[memory][object]") {
    engine::memory::objectAllocator<Counted> heap;
    REQUIRE_FALSE(heap.pooled());
    REQUIRE(heap.capacity() == std::numeric_limits<std::size_t>::max());

    std::vector<Counted*> objects;
    for (int i = 0; i < 100; ++i) {
        objects.push_back(heap.create(i));
    }
    REQUIRE(heap.size() == 100);
    REQUIRE_FALSE(heap.full());
    REQUIRE(objects[42]->value == 42);

    for (Counted* o : objects) {
        heap.destroy(o);
    }
    REQUIRE(heap.size() == 0);
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("objectAllocator in pool mode stops at its capacity", "[memory][object]") {
    engine::memory::objectAllocator<Counted> pooled(2);
    REQUIRE(pooled.pooled());
    REQUIRE(pooled.capacity() == 2);

    Counted* a = pooled.create(1);
    Counted* b = pooled.create(2);
    REQUIRE(pooled.full());
    REQUIRE_THROWS_AS(pooled.create(3), std::length_error);

    pooled.destroy(a);
    Counted* c = pooled.create(3);
    REQUIRE(c == a);

    pooled.destroy(b);
    pooled.destroy(c);
    pooled.destroy(nullptr);
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("objectAllocator move leaves the source an empty heap allocator", "[memory][object]") {
    engine::memory::objectAllocator<Counted> pooled(2);
    Counted* a = pooled.create(1);

    engine::memory::objectAllocator<Counted> moved(std::move(pooled));

    REQUIRE(moved.pooled());
    REQUIRE(moved.size() == 1);
    REQUIRE_FALSE(pooled.pooled());
    REQUIRE(pooled.size() == 0);
    moved.destroy(a);
}

TEST_CASE("componentTable behaves like a map from EntityId", "[memory][table]") {
    engine::componentTable<std::string> table;
    table.try_emplace(1, "one");
    table.insert_or_assign(2, std::string("two"));

    REQUIRE(table.size() == 2);
    REQUIRE(table.contains(1));
    REQUIRE(table.count(3) == 0);
    REQUIRE(table.find(2)->second == "two");
    REQUIRE(table.find(3) == table.end());

    std::string* first = &table.find(1)->second;
    auto [it, added] = table.insert_or_assign(1, std::string("uno"));
    REQUIRE_FALSE(added);
    REQUIRE(&it->second == first); // overwritten in place
    REQUIRE(*first == "uno");

    std::set<engine::EntityId> seen;
    for (auto& [id, value] : table) {
        seen.insert(id);
        value += "!";
    }
    REQUIRE(seen == std::set<engine::EntityId>{1, 2});
    REQUIRE(table.find(2)->second == "two!");

    REQUIRE(table.erase(1) == 1);
    REQUIRE(table.erase(1) == 0);
    REQUIRE(table.size() == 1);
}

TEST_CASE("componentTable::erase(iterator) can remove while iterating", "[memory][table]") {
    engine::componentTable<Counted> table(8);
    for (engine::EntityId id = 1; id <= 6; ++id) {
        table.try_emplace(id, static_cast<int>(id));
    }

    for (auto it = table.begin(); it != table.end();) {
        it = it->second.value % 2 == 0 ? table.erase(it) : std::next(it);
    }

    REQUIRE(table.size() == 3);
    for (const auto& [id, c] : table) {
        REQUIRE(c.value % 2 == 1);
    }
    table.clear();
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("A pooled componentTable stops at its capacity", "[memory][table]") {
    engine::componentTable<Counted> table(2);
    REQUIRE(table.pooled());
    REQUIRE(table.capacity() == 2);

    table.try_emplace(1, 1);
    table.try_emplace(2, 2);
    REQUIRE(table.full());
    REQUIRE_THROWS_AS(table.try_emplace(3, 3), std::length_error);
    REQUIRE(table.size() == 2);

    table.erase(1);
    REQUIRE_NOTHROW(table.try_emplace(3, 3));
}

TEST_CASE("componentTable copies are deep and keep the pool settings", "[memory][table]") {
    engine::componentTable<Counted> table(4);
    table.try_emplace(1, 10);

    engine::componentTable<Counted> copy = table;
    REQUIRE(copy.pooled());
    REQUIRE(copy.capacity() == 4);
    REQUIRE(&copy.find(1)->second != &table.find(1)->second);

    copy.find(1)->second.value = 99;
    copy.try_emplace(2, 20);
    REQUIRE(table.find(1)->second.value == 10);
    REQUIRE(table.size() == 1);

    engine::componentTable<Counted> heap;
    heap = table;
    REQUIRE(heap.pooled());
    REQUIRE(heap.find(1)->second.value == 10);
}

TEST_CASE("componentTable entries stay put when the table moves", "[memory][table]") {
    engine::componentTable<Counted> table(4);
    Counted* entry = &table.try_emplace(1, 5).first->second;

    engine::componentTable<Counted> moved(std::move(table));
    REQUIRE(&moved.find(1)->second == entry);
    REQUIRE(table.empty());

    table = std::move(moved);
    REQUIRE(&table.find(1)->second == entry);
}

// ---- poolAllocator edge cases ----------------------------------------------------------------

TEST_CASE("poolAllocator with capacity 0 is always full", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(0);
    REQUIRE(pool.capacity() == 0);
    REQUIRE(pool.full());
    REQUIRE_THROWS_AS(pool.create(1), std::length_error);
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("poolAllocator::destroy(nullptr) is a no-op", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(1);
    pool.destroy(nullptr);
    REQUIRE(pool.size() == 0);
}

TEST_CASE("poolAllocator prefers a freed slot over an untouched one", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> pool(4);
    Counted* a = pool.create(1);
    Counted* b = pool.create(2);
    Counted* c = pool.create(3);

    pool.destroy(b);
    pool.destroy(a);
    // Most recently freed first, then the older one, then the untouched fourth slot.
    REQUIRE(pool.create(4) == a);
    REQUIRE(pool.create(5) == b);
    Counted* d = pool.create(6);
    REQUIRE(d != a);
    REQUIRE(d != b);
    REQUIRE(d != c);
    REQUIRE(pool.owns(d));
    REQUIRE(pool.full());

    pool.destroy(a);
    pool.destroy(b);
    pool.destroy(c);
    pool.destroy(d);
}

TEST_CASE("poolAllocator::owns rejects slot interiors and untouched slots", "[memory][pool]") {
    engine::memory::poolAllocator<std::uint64_t> pool(3);
    std::uint64_t* first = pool.create(std::uint64_t{1});

    const auto* interior =
        reinterpret_cast<const std::uint64_t*>(reinterpret_cast<const std::byte*>(first) + 1);
    REQUIRE_FALSE(pool.owns(interior));
    REQUIRE_FALSE(pool.owns(first + 1)); // the next slot, never handed out yet

    pool.destroy(first);
}

TEST_CASE("poolAllocator objects smaller than a pointer don't overlap", "[memory][pool]") {
    // Each free slot stores a pointer, so slots must be at least pointer-sized.
    engine::memory::poolAllocator<std::uint8_t> pool(8);
    std::vector<std::uint8_t*> bytes;
    for (std::uint8_t i = 0; i < 8; ++i) {
        bytes.push_back(pool.create(static_cast<std::uint8_t>(0xA0 + i)));
    }

    pool.destroy(bytes[3]);
    pool.destroy(bytes[5]);
    bytes[3] = pool.create(std::uint8_t{0x33});

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 5) {
            continue;
        }
        INFO("slot " << i);
        const unsigned expected = i == 3 ? 0x33u : 0xA0u + static_cast<unsigned>(i);
        REQUIRE(static_cast<unsigned>(*bytes[i]) == expected);
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 5) {
            pool.destroy(bytes[i]);
        }
    }
}

TEST_CASE("poolAllocator survives random create/destroy churn", "[memory][pool]") {
    constexpr std::size_t capacity = 64;
    engine::memory::poolAllocator<Counted> pool(capacity);
    std::vector<Counted*> live;
    std::mt19937 rng(481);
    int nextValue = 0;

    for (int step = 0; step < 20'000; ++step) {
        const bool create = live.empty() || (live.size() < capacity && rng() % 2 == 0);
        if (create) {
            live.push_back(pool.create(nextValue++));
        } else {
            const std::size_t victim = rng() % live.size();
            pool.destroy(live[victim]);
            live[victim] = live.back();
            live.pop_back();
        }
        REQUIRE(pool.size() == live.size());
    }

    // Every live object is in the pool, unique, and still holds a value it was given.
    REQUIRE(std::set<Counted*>(live.begin(), live.end()).size() == live.size());
    std::set<int> values;
    for (Counted* c : live) {
        REQUIRE(pool.owns(c));
        values.insert(c->value);
    }
    REQUIRE(values.size() == live.size());
    REQUIRE(Counted::alive == static_cast<int>(live.size()));

    for (Counted* c : live) {
        pool.destroy(c);
    }
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("poolAllocator move assignment and swap carry the objects", "[memory][pool]") {
    engine::memory::poolAllocator<Counted> a(2);
    engine::memory::poolAllocator<Counted> b(5);
    Counted* inA = a.create(1);

    b = std::move(a);
    REQUIRE(b.capacity() == 2);
    REQUIRE(b.owns(inA));
    REQUIRE(a.capacity() == 0);

    engine::memory::poolAllocator<Counted> c(3);
    c.swap(b);
    REQUIRE(c.owns(inA));
    REQUIRE(c.capacity() == 2);
    REQUIRE(b.capacity() == 3);
    REQUIRE(b.size() == 0);

    c.destroy(inA);
}

// ---- objectAllocator edge cases --------------------------------------------------------------

TEST_CASE("objectAllocator with a 0-object pool is full from the start", "[memory][object]") {
    engine::memory::objectAllocator<Counted> pooled(0);
    REQUIRE(pooled.pooled());
    REQUIRE(pooled.full());
    REQUIRE_THROWS_AS(pooled.create(1), std::length_error);
}

TEST_CASE("objectAllocator move assignment and swap exchange modes", "[memory][object]") {
    engine::memory::objectAllocator<Counted> heap;
    engine::memory::objectAllocator<Counted> pooled(3);
    Counted* fromHeap = heap.create(1);
    Counted* fromPool = pooled.create(2);

    heap.swap(pooled);
    REQUIRE(heap.pooled());
    REQUIRE(heap.capacity() == 3);
    REQUIRE(heap.size() == 1);
    REQUIRE_FALSE(pooled.pooled());
    REQUIRE(pooled.size() == 1);

    heap.destroy(fromPool);
    pooled.destroy(fromHeap);

    engine::memory::objectAllocator<Counted> target;
    target = std::move(heap);
    REQUIRE(target.pooled());
    REQUIRE_FALSE(heap.pooled());
    REQUIRE(Counted::alive == 0);
}

// ---- componentTable edge cases ---------------------------------------------------------------

TEST_CASE("componentTable::try_emplace leaves an existing entry alone", "[memory][table]") {
    engine::componentTable<Counted> table(2);
    Counted* first = &table.try_emplace(1, 10).first->second;

    auto [it, added] = table.try_emplace(1, 20);
    REQUIRE_FALSE(added);
    REQUIRE(&it->second == first);
    REQUIRE(first->value == 10);
    REQUIRE(Counted::alive == 1); // nothing was built for the rejected value
    REQUIRE(table.size() == 1);
}

TEST_CASE("componentTable overwrites in a full pool without a new slot", "[memory][table]") {
    engine::componentTable<Counted> table(1);
    table.try_emplace(1, 1);
    REQUIRE(table.full());

    REQUIRE_NOTHROW(table.insert_or_assign(1, Counted(5)));
    REQUIRE(table.find(1)->second.value == 5);
    REQUIRE_THROWS_AS(table.insert_or_assign(2, Counted(6)), std::length_error);
}

TEST_CASE("componentTable::clear keeps the pool and frees every slot", "[memory][table]") {
    engine::componentTable<Counted> table(3);
    for (engine::EntityId id = 1; id <= 3; ++id) {
        table.try_emplace(id, 0);
    }
    REQUIRE(table.full());

    table.clear();
    REQUIRE(table.empty());
    REQUIRE(table.pooled());
    REQUIRE(table.capacity() == 3);
    REQUIRE(Counted::alive == 0);

    for (engine::EntityId id = 10; id <= 12; ++id) {
        REQUIRE_NOTHROW(table.try_emplace(id, 0));
    }
}

TEST_CASE("componentTable destroys its entries when it goes away", "[memory][table]") {
    {
        engine::componentTable<Counted> pooled(4);
        engine::componentTable<Counted> heap;
        pooled.try_emplace(1, 1);
        pooled.try_emplace(2, 2);
        heap.try_emplace(1, 1);
        REQUIRE(Counted::alive == 3);
    }
    REQUIRE(Counted::alive == 0);
}

TEST_CASE("componentTable copies follow the source's mode", "[memory][table]") {
    engine::componentTable<Counted> heap;
    heap.try_emplace(1, 1);
    engine::componentTable<Counted> heapCopy = heap;
    REQUIRE_FALSE(heapCopy.pooled());
    REQUIRE(heapCopy.find(1)->second.value == 1);

    engine::componentTable<Counted> full(2);
    full.try_emplace(1, 1);
    full.try_emplace(2, 2);
    engine::componentTable<Counted> fullCopy = full;
    REQUIRE(fullCopy.full());
    REQUIRE_THROWS_AS(fullCopy.try_emplace(3, 3), std::length_error);
}

TEST_CASE("componentTable self-assignment is harmless", "[memory][table]") {
    engine::componentTable<Counted> table(2);
    table.try_emplace(1, 7);
    Counted* entry = &table.find(1)->second;

    auto& self = table; // avoids -Wself-assign
    table = self;

    REQUIRE(table.size() == 1);
    REQUIRE(&table.find(1)->second == entry);
    REQUIRE(entry->value == 7);
}

TEST_CASE("componentTable can be read through a const reference", "[memory][table]") {
    engine::componentTable<Counted> table;
    table.try_emplace(1, 1);
    table.try_emplace(2, 2);
    const engine::componentTable<Counted>& view = table;

    REQUIRE(view.find(2)->second.value == 2);
    REQUIRE(view.find(3) == view.end());
    REQUIRE(view.contains(1));

    int sum = 0;
    for (const auto& [id, c] : view) {
        sum += c.value;
    }
    REQUIRE(sum == 3);

    // iterator converts to const_iterator and compares with it
    engine::componentTable<Counted>::const_iterator it = table.find(1);
    REQUIRE(it == view.find(1));
    REQUIRE(it != view.end());
}

TEST_CASE("componentTable entries keep their address as the table grows", "[memory][table]") {
    SECTION("heap mode, through rehashes") {
        engine::componentTable<Counted> table;
        Counted* first = &table.try_emplace(1, 42).first->second;
        for (engine::EntityId id = 2; id <= 1000; ++id) {
            table.try_emplace(id, 0);
        }
        REQUIRE(&table.find(1)->second == first);
        REQUIRE(first->value == 42);
    }
    SECTION("pool mode") {
        engine::componentTable<Counted> table(1000);
        Counted* first = &table.try_emplace(1, 42).first->second;
        for (engine::EntityId id = 2; id <= 1000; ++id) {
            table.try_emplace(id, 0);
        }
        REQUIRE(&table.find(1)->second == first);
        REQUIRE(first->value == 42);
    }
}

TEST_CASE("A pooled componentTable keeps its entries in one block", "[memory][table]") {
    constexpr std::size_t capacity = 32;
    engine::componentTable<Counted> table(capacity);
    std::vector<std::uintptr_t> addresses;
    for (engine::EntityId id = 1; id <= capacity; ++id) {
        addresses.push_back(reinterpret_cast<std::uintptr_t>(&*table.try_emplace(id, 0).first));
    }

    const auto [low, high] = std::minmax_element(addresses.begin(), addresses.end());
    using entry = engine::componentTable<Counted>::value_type;
    const std::size_t slotSize = std::max(sizeof(entry), sizeof(void*));
    REQUIRE(*high - *low == (capacity - 1) * slotSize); // packed back to back, no gaps
}
