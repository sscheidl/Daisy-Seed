#pragma once
// SPDX-License-Identifier: MIT
// Grain state and the pre-reserved pool: a free stack, with the active grains on one singly
// linked list per voice. Capacity is a hard invariant; nothing allocates. B6: a grain has no
// window pointer or shape, only win_phase and win_step.
#include "params.h"
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

constexpr uint16_t kNoGrain = 0xFFFF;
constexpr uint8_t kGrainWraps = 1, kGrainSampleSlot = 2;

struct Grain {
    int64_t pos = 0, inc = 0;             // Q32.32 frames; inc is negative for reverse grains
    uint32_t win_phase = 0, win_step = 0; // whole window = 2^32
    float gain_l = 0.f, gain_r = 0.f;     // pan and PCM scale, fixed at birth
    uint16_t next = kNoGrain, generation = 0;
    uint16_t delay = 0;                   // frames until the first output, inside the birth block
    uint8_t voice = 0, flags = 0;
};
static_assert(sizeof(Grain) <= 48, "256 grains must stay small enough for internal RAM");

class GrainPool {
  public:
    void Prepare(std::size_t capacity) {
        capacity_ = capacity > kMaxGrains ? kMaxGrains : capacity;
        free_count_ = 0;
        for(std::size_t i = capacity_; i > 0; --i)
            free_[free_count_++] = uint16_t(i - 1);
    }
    uint16_t Acquire() { return free_count_ ? free_[--free_count_] : kNoGrain; }
    void Release(uint16_t index) { free_[free_count_++] = index; }
    Grain& At(uint16_t index) { return grains_[index]; }
    const Grain& At(uint16_t index) const { return grains_[index]; }
    std::size_t Capacity() const { return capacity_; }
    std::size_t Free() const { return free_count_; }
    std::size_t Active() const { return capacity_ - free_count_; }

  private:
    Grain grains_[kMaxGrains]{};
    uint16_t free_[kMaxGrains]{};
    std::size_t capacity_ = 0, free_count_ = 0;
};

} // namespace grain_cloud
