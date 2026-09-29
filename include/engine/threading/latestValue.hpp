#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace engine::threading {

/** Single-slot mailbox where a publish replaces any unconsumed value. For state where only the
 *  newest value matters: input snapshots main to sim, scene snapshots sim to main.
 *
 *  @thread_safety All members are safe to call concurrently from any thread. */
template <typename T> class LatestValue {
public:
    LatestValue() = default;
    LatestValue(const LatestValue&) = delete;
    LatestValue& operator=(const LatestValue&) = delete;
    LatestValue(LatestValue&&) = delete;
    LatestValue& operator=(LatestValue&&) = delete;

    /** Taken by value so any copy happens before the lock. */
    void publish(T value) {
        std::lock_guard lock(mutex_);
        if (slot_) {
            overwritten_.fetch_add(1, std::memory_order_relaxed);
        }
        slot_ = std::move(value);
        published_.fetch_add(1, std::memory_order_relaxed);
    }

    /** Returns and clears the pending value; nullopt if none. */
    std::optional<T> take() {
        std::lock_guard lock(mutex_);
        std::optional<T> value = std::move(slot_);
        slot_.reset();
        return value;
    }

    bool hasPending() const {
        std::lock_guard lock(mutex_);
        return slot_.has_value();
    }

    std::uint64_t published() const noexcept { return published_.load(std::memory_order_relaxed); }

    /** publish() calls that replaced an unconsumed value. */
    std::uint64_t overwritten() const noexcept {
        return overwritten_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex mutex_;
    std::optional<T> slot_;
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> overwritten_{0};
};

} // namespace engine::threading
