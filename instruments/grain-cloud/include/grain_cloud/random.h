#pragma once
// SPDX-License-Identifier: MIT
// The random generator every voice owns: a 32-bit linear congruential step, one multiply.
// Only the top bits are used. B10: triangular spray is the sum of two draws, no table.
#include <cstdint>

namespace grain_cloud {

class GrainRng {
  public:
    GrainRng() = default;
    explicit GrainRng(uint32_t seed) : state_(seed) {}
    void Seed(uint32_t seed) { state_ = seed; }
    uint32_t Next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }
    float Unit() { return float(Next() >> 8) * (1.f / 16777216.f); } // 0 .. <1
    float Bipolar() { return Unit() * 2.f - 1.f; }                   // -1 .. <1
    float Triangular() {                                             // -1 .. <1, peak at 0
        const float a = Unit();
        return a + Unit() - 1.f;
    }

  private:
    uint32_t state_ = 1u;
};

} // namespace grain_cloud
