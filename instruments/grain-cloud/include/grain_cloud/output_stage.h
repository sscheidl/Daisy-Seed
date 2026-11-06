#pragma once
// SPDX-License-Identifier: MIT
// R5: the one clip point. The product calls it last, after its effect chain. Everything
// before it sums in float without any clamp. Below the knee the signal passes unchanged.
#include "laws.h"
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

constexpr float kClipKnee = 0.8f;

inline float SoftClip(float x) {
    const float magnitude = x < 0.f ? -x : x;
    if(magnitude <= kClipKnee)
        return x;
    const float over = (magnitude - kClipKnee) * (1.f / (1.f - kClipKnee));
    const float y = kClipKnee + (1.f - kClipKnee) * over / (1.f + over);
    return x < 0.f ? -y : y;
}

class OutputStage {
  public:
    void Prepare(float sample_rate) {
        ramp_frames_ = uint32_t(sample_rate * 0.01f);
        master_.Reset(1.f);
        peak_in_ = peak_out_ = 0.f;
        clipped_ = non_finite_ = 0;
    }
    void SetMaster(float gain) { master_.Set(gain, ramp_frames_); }

    // Exact in-place use is the normal case.
    void Process(const float* in_left, const float* in_right, float* out_left, float* out_right,
                 std::size_t frames) {
        for(std::size_t i = 0; i < frames; ++i) {
            const float gain = master_.Next();
            out_left[i] = One(in_left[i] * gain);
            out_right[i] = One(in_right[i] * gain);
        }
    }
    float PeakIn() const { return peak_in_; }
    float PeakOut() const { return peak_out_; }
    uint32_t Clipped() const { return clipped_; }
    uint32_t NonFinite() const { return non_finite_; }

  private:
    float One(float x) {
        if(!(x - x == 0.f)) { // NaN or infinity
            ++non_finite_;
            return 0.f;
        }
        const float magnitude = x < 0.f ? -x : x;
        if(magnitude > peak_in_)
            peak_in_ = magnitude;
        if(magnitude > kClipKnee)
            ++clipped_;
        const float y = SoftClip(x);
        const float out = y < 0.f ? -y : y;
        if(out > peak_out_)
            peak_out_ = out;
        return y;
    }
    Ramp master_;
    uint32_t ramp_frames_ = 480, clipped_ = 0, non_finite_ = 0;
    float peak_in_ = 0.f, peak_out_ = 0.f;
};

} // namespace grain_cloud
