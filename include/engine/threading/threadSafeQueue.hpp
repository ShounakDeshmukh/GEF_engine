#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace engine::threading {

/** FIFO queue, optionally bounded, that drops its oldest element when full. For ordered messages:
 *  tasks main to sim, per-tick outbound state sim to net, inbound net messages net to sim.
 *
 *  @thread_safety All members are safe to call concurrently from any thread. */
template <typename T> class ThreadSafeQueue {
public:
    /** capacity 0 means unbounded. */
    explicit ThreadSafeQueue(std::size_t capacity = 0) : capacity_(capacity) {}
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    void push(T value) {
        {
            std::lock_guard lock(mutex_);
            if (capacity_ != 0 && queue_.size() == capacity_) {
                queue_.pop_front();
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            queue_.push_back(std::move(value));
        }
        ready_.notify_one();
    }

    std::optional<T> tryPop() {
        std::lock_guard lock(mutex_);
        return popLocked();
    }

    /** Blocks until an element arrives or timeout elapses. */
    std::optional<T> waitPop(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, timeout, [this] { return !queue_.empty(); });
        return popLocked();
    }

    /** Swaps the whole queue out, so the lock is held for O(1). */
    std::deque<T> drainAll() {
        std::deque<T> drained;
        std::lock_guard lock(mutex_);
        drained.swap(queue_);
        return drained;
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    /** Elements discarded to the capacity bound. */
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    std::optional<T> popLocked() {
        if (queue_.empty()) {
            return std::nullopt;
        }
        std::optional<T> value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> queue_;
    const std::size_t capacity_;
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace engine::threading
