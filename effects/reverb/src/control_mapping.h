#pragma once
// SPDX-License-Identifier: MIT
#include <cmath>

namespace ambient {
// Shared musical control ranges. Hosts own physical pot assignments.
constexpr float kMaximumModDepthMs = 3.f * 1.33f;
constexpr float kMaximumTexture = 1.25f;
constexpr float kMaximumRiseSeconds = 5.f;
// Cloud hosts without a Sustain button also use X to scale modulation depth.
constexpr float kMaximumCloudDepthScale = 1.25f;
constexpr float kMaximumEffectiveModDepthMs =
    kMaximumModDepthMs * kMaximumCloudDepthScale;

inline float ModDepthFromControl(float value) { return kMaximumModDepthMs * value; }
inline float TextureFromControl(float value) { return kMaximumTexture * value; }
inline float RiseFromControl(float value) {
    return .05f * std::pow(kMaximumRiseSeconds / .05f, value);
}
inline float CloudDepthScaleFromControl(float value) {
    return .5f + (kMaximumCloudDepthScale - .5f) * value;
}
} // namespace ambient
