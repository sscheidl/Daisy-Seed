#pragma once
// Lock-free triple buffer: one writer, one reader, neither ever waits and the
// reader always gets the newest complete value (ARCHITECTURE.md § 3.5).
#include <atomic>
#include <cstdint>

namespace mod {
template<class T> class TripleBuffer {
    static_assert(std::atomic<uint8_t>::is_always_lock_free, "triple buffer index must be lock-free");
  public:
    // Writer side.
    T& Back() { return buffers_[back_]; }
    void Publish() {
        const uint8_t old = middle_.exchange(uint8_t(back_ | kFresh), std::memory_order_acq_rel);
        back_ = uint8_t(old & kIndex);
    }
    // Reader side: true if a newer value was taken over.
    bool Fetch() {
        if(!(middle_.load(std::memory_order_relaxed) & kFresh))
            return false;
        const uint8_t old = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = uint8_t(old & kIndex);
        return true;
    }
    const T& Front() const { return buffers_[front_]; }
  private:
    static constexpr uint8_t kFresh = 0x80, kIndex = 0x03;
    T buffers_[3]{};
    uint8_t back_ = 0, front_ = 1;
    std::atomic<uint8_t> middle_{2};
};
}
