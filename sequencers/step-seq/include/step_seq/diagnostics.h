#pragma once
// SPDX-License-Identifier: MIT
// Callback diagnostics for the resource PASS (ARBEITSAUFTRAG 5, R15): callback peak and
// deadline overruns from a cycle counter, and the stack high-water mark from a painted
// region. Values are evidence on the kit only; no CPU percentages from host timings.
#include <atomic>
#include <cstdint>

namespace step_seq {

class CallbackMeter {
  public:
    void Init(uint32_t budget_cycles) { budget_ = budget_cycles; }
    void Begin(uint32_t cycles) { start_ = cycles; }
    void End(uint32_t cycles) {
        const uint32_t elapsed = cycles - start_;
        if(elapsed > peak_.load(std::memory_order_relaxed))
            peak_.store(elapsed, std::memory_order_relaxed);
        if(elapsed > budget_)
            overruns_.fetch_add(1, std::memory_order_relaxed);
        blocks_.fetch_add(1, std::memory_order_relaxed);
    }
    uint32_t Peak() const { return peak_.load(std::memory_order_relaxed); }
    uint32_t Overruns() const { return overruns_.load(std::memory_order_relaxed); }
    uint32_t Blocks() const { return blocks_.load(std::memory_order_relaxed); }
    uint32_t Budget() const { return budget_; }
    void Reset() {
        peak_.store(0, std::memory_order_relaxed);
        overruns_.store(0, std::memory_order_relaxed);
    }

  private:
    uint32_t budget_ = 0, start_ = 0;
    std::atomic<uint32_t> peak_{0}, overruns_{0}, blocks_{0};
};

// Fill [low, high) with a pattern before the audio starts; the free words at the bottom
// that still hold it give the stack reserve that was never used.
class StackProbe {
  public:
    static constexpr uint32_t kPattern = 0x5A5AA5A5u;
    static void Paint(uint32_t* low, uint32_t* high) {
        for(volatile uint32_t* p = low; p < high; ++p)
            *p = kPattern;
    }
    static uint32_t FreeBytes(const uint32_t* low, const uint32_t* high) {
        uint32_t words = 0;
        for(const volatile uint32_t* p = low; p < high && *p == kPattern; ++p)
            ++words;
        return words * 4u;
    }
};

} // namespace step_seq
