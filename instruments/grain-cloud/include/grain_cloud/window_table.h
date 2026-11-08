#pragma once
// SPDX-License-Identifier: MIT
// B6: one window table per voice group. A grain holds only its phase and step and indexes
// this table, so a morph reaches running grains too. The control tick is the only writer
// and never overlaps rendering (R1), so one live table is enough. The morph position is
// slew-limited, which bounds the change per tick; the work per tick is one pass at most.
#include "laws.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

constexpr std::size_t kWindowSegments = 256, kWindowPoints = kWindowSegments + 1;
constexpr std::size_t kWindowShapes = 4;
constexpr float kWindowMorphSeconds = 1.f; // full range; slow on purpose

// The phase is the whole window in 32 bits, so a grain ends when it carries out.
inline float WindowAt(const float* table, uint32_t phase) {
    const uint32_t index = phase >> 24;
    const float fraction = float(phase & 0xFFFFFFu) * (1.f / 16777216.f);
    const float a = table[index];
    return a + fraction * (table[index + 1] - a);
}

class WindowTable {
  public:
    // Startup only: trigonometry stays out of the tick and the render loop.
    void Prepare(float tick_seconds) {
        constexpr float kTwoPi = 6.28318530718f;
        for(std::size_t i = 0; i < kWindowPoints; ++i) {
            const float u = float(i) / float(kWindowSegments);
            const float edge = u < 0.5f ? u : 1.f - u;
            shapes_[0][i] = 0.5f - 0.5f * std::cos(kTwoPi * u); // Hann
            // Flat top with a quarter of the window as taper on each side.
            shapes_[1][i] = edge >= 0.25f ? 1.f : 0.5f - 0.5f * std::cos(kTwoPi * edge * 2.f);
            shapes_[2][i] = 2.f * edge; // triangle
            // Percussive: short rise, long fall.
            const float rise = u < 0.0625f ? 0.5f - 0.5f * std::cos(kTwoPi * u * 8.f) : 1.f;
            shapes_[3][i] = rise * (1.f - u) * (1.f - u);
        }
        for(auto& shape : shapes_)
            shape[0] = shape[kWindowSegments] = 0.f; // exact zeros, so every morph ends at zero
        slew_ = tick_seconds / kWindowMorphSeconds;
        morph_ = target_ = 0.f;
        Rebuild();
    }
    void SetTarget(float morph) { target_ = Clamp01(morph); }

    // Control tick. Returns the points written: 0 when the morph rests.
    std::size_t Tick() {
        if(morph_ == target_)
            return 0;
        const float delta = target_ - morph_;
        morph_ = delta > slew_ ? morph_ + slew_ : (delta < -slew_ ? morph_ - slew_ : target_);
        Rebuild();
        return kWindowPoints;
    }
    const float* Data() const { return live_; }
    float Morph() const { return morph_; }
    float Target() const { return target_; }
    const float* Shape(std::size_t index) const { return shapes_[index]; }

  private:
    void Rebuild() {
        const float scaled = morph_ * float(kWindowShapes - 1);
        std::size_t base = std::size_t(scaled);
        if(base > kWindowShapes - 2)
            base = kWindowShapes - 2;
        const float fraction = scaled - float(base);
        const float* a = shapes_[base];
        const float* b = shapes_[base + 1];
        for(std::size_t i = 0; i < kWindowPoints; ++i)
            live_[i] = a[i] + fraction * (b[i] - a[i]);
    }

    float shapes_[kWindowShapes][kWindowPoints]{};
    float live_[kWindowPoints]{};
    float morph_ = 0.f, target_ = 0.f, slew_ = 1.f;
};

} // namespace grain_cloud
