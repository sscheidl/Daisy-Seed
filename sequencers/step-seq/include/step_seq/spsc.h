#pragma once
// SPDX-License-Identifier: MIT
// Lock-free hand-over between the main loop and the audio callback (ARBEITSAUFTRAG 3.2,
// R10): a single-producer/single-consumer queue and a triple buffer for snapshots.
// No allocation, no locks; safe between one interrupt and one thread.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace step_seq {

template <class T, std::size_t N>
class SpscQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "capacity must be a power of two");

  public:
    // Producer only. false: full, nothing written.
    bool Push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if(head - tail_.load(std::memory_order_acquire) >= N)
            return false;
        items_[head & (N - 1)] = value;
        head_.store(head + 1, std::memory_order_release);
        const std::size_t used = head + 1 - tail_.load(std::memory_order_relaxed);
        if(used > high_water_.load(std::memory_order_relaxed))
            high_water_.store(used, std::memory_order_relaxed);
        return true;
    }
    // Consumer only. false: empty.
    bool Pop(T& value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire))
            return false;
        value = items_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    // Consumer only: the oldest item without removing it.
    const T* Peek() const {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire))
            return nullptr;
        return &items_[tail & (N - 1)];
    }
    std::size_t Size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    std::size_t HighWater() const { return high_water_.load(std::memory_order_relaxed); }
    static constexpr std::size_t Capacity() { return N; }

  private:
    std::array<T, N> items_{};
    std::atomic<std::size_t> head_{0}, tail_{0}, high_water_{0};
};

// One writer publishes whole snapshots, one reader always gets the latest complete one.
template <class T>
class TripleBuffer {
  public:
    // Writer only: the buffer to fill, then Publish().
    T& Back() { return slots_[back_]; }
    void Publish() {
        const uint8_t old = middle_.exchange(uint8_t(back_ | kFresh), std::memory_order_acq_rel);
        back_ = old & kIndex;
    }
    // Reader only: true if a newer snapshot arrived since the last call.
    bool Update() {
        if(!(middle_.load(std::memory_order_acquire) & kFresh))
            return false;
        const uint8_t old = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = old & kIndex;
        return true;
    }
    const T& Front() const { return slots_[front_]; }

  private:
    static constexpr uint8_t kIndex = 3, kFresh = 4;
    std::array<T, 3> slots_{};
    uint8_t back_ = 0, front_ = 1;
    std::atomic<uint8_t> middle_{2};
};

} // namespace step_seq
