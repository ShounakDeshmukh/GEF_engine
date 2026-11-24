// Counts heap allocations to show where the pool allocator avoids them. This file replaces the
// global operator new, so it builds into its own executable (engine_alloc_tests) instead of
// engine_tests, where networking threads would add to the count.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdlib>
#include <engine/componentTable.hpp>
#include <engine/entity.hpp>
#include <engine/memory/objectAllocator.hpp>
#include <engine/memory/poolAllocator.hpp>
#include <new>
#include <optional>
#include <vector>

namespace {
std::atomic<std::size_t> heapAllocations{0};

void* countedAllocate(std::size_t size) {
    heapAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size) {
    return countedAllocate(size);
}
void* operator new[](std::size_t size) {
    return countedAllocate(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

/** The number of operator new calls made while f runs. */
template <typename F> std::size_t allocationsDuring(F&& f) {
    const std::size_t before = heapAllocations.load();
    f();
    return heapAllocations.load() - before;
}

struct Particle {
    float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f;
};

struct Health {
    int hp = 0;
};

} // namespace

TEST_CASE("poolAllocator allocates once, up front", "[alloc]") {
    constexpr std::size_t count = 500;
    std::optional<engine::memory::poolAllocator<Particle>> pool;
    std::vector<Particle*> particles;
    particles.reserve(count);

    REQUIRE(allocationsDuring([&] { pool.emplace(count); }) == 1);

    // Two full rounds of create and destroy, with no heap traffic at all.
    const std::size_t churn = allocationsDuring([&] {
        for (int round = 0; round < 2; ++round) {
            for (std::size_t i = 0; i < count; ++i) {
                particles.push_back(pool->create());
            }
            for (Particle* p : particles) {
                pool->destroy(p);
            }
            particles.clear();
        }
    });
    REQUIRE(churn == 0);
}

TEST_CASE("objectAllocator allocates per object only in heap mode", "[alloc]") {
    constexpr std::size_t count = 100;
    std::vector<Particle*> particles;
    particles.reserve(count);

    engine::memory::objectAllocator<Particle> heap;
    REQUIRE(allocationsDuring([&] {
                for (std::size_t i = 0; i < count; ++i) {
                    particles.push_back(heap.create());
                }
            }) == count);
    for (Particle* p : particles) {
        heap.destroy(p);
    }
    particles.clear();

    engine::memory::objectAllocator<Particle> pooled(count);
    REQUIRE(allocationsDuring([&] {
                for (std::size_t i = 0; i < count; ++i) {
                    particles.push_back(pooled.create());
                }
            }) == 0);
    for (Particle* p : particles) {
        pooled.destroy(p);
    }
}

TEST_CASE("A pooled componentTable allocates only its index nodes", "[alloc]") {
    constexpr engine::EntityId count = 200;
    engine::componentTable<Particle> pooled(count);
    engine::componentTable<Particle> heap;

    const std::size_t pooledAllocations = allocationsDuring([&] {
        for (engine::EntityId id = 1; id <= count; ++id) {
            pooled.try_emplace(id);
        }
    });
    const std::size_t heapAllocationCount = allocationsDuring([&] {
        for (engine::EntityId id = 1; id <= count; ++id) {
            heap.try_emplace(id);
        }
    });

    // Pooled: the entries come from the pool; the id-to-entry index still adds a node each.
    REQUIRE(pooledAllocations <= count);
    // Heap: an entry and an index node each, plus the index's rehashes.
    REQUIRE(heapAllocationCount >= 2 * count);
}

TEST_CASE("A pooled Scene adds entities and components without allocating them", "[alloc]") {
    constexpr std::size_t count = 100;
    engine::Scene scene;
    scene.reserveEntities(count);
    scene.reservePool<Health>(count);

    std::vector<engine::EntityId> ids;
    ids.reserve(count);
    const std::size_t pooledAllocations = allocationsDuring([&] {
        for (std::size_t i = 0; i < count; ++i) {
            const engine::EntityId id = scene.createEntity();
            scene.add<Health>(id, {10});
            ids.push_back(id);
        }
    });
    REQUIRE(pooledAllocations <= 2 * count); // only the Transform and Health index nodes

    engine::Scene heapScene;
    const std::size_t heapAllocationCount = allocationsDuring([&] {
        for (std::size_t i = 0; i < count; ++i) {
            heapScene.add<Health>(heapScene.createEntity(), {10});
        }
    });
    REQUIRE(heapAllocationCount >= 4 * count);

    // Destroying entities in a pooled Scene never allocates.
    REQUIRE(allocationsDuring([&] {
                for (engine::EntityId id : ids) {
                    scene.destroyEntity(id);
                }
            }) == 0);
}
