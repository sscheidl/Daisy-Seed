#pragma once
// SPDX-License-Identifier: MIT
// Host side of a standalone wet-only effect under the FX contract 1.0: the feed fades over
// 10 ms when switched, so the tail rings on, and dry/wet glides over 10 ms. A mix of 0 gives
// the exact dry signal. Effects with their own tail policy keep their own host (stereo delay).
#include "fx_contract.h"
#include <cstddef>
#define SEED_WET_HOST_VERSION "0.1.0"

namespace seed::audio {

template <class Effect, std::size_t MaxBlock> class WetHost {
  public:
    explicit WetHost(Effect& effect) : effect_(effect) {}

    void Reset(bool enabled, float mix) {
        enabled_ = enabled;
        feed_ = enabled ? 1.f : 0.f;
        SetMix(mix);
        mix_ = mix_target_;
    }
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool Enabled() const { return enabled_; }
    void SetMix(float mix) {
        if(mix == mix) // not NaN
            mix_target_ = mix < 0.f ? 0.f : (mix > 1.f ? 1.f : mix);
    }

    // Exact in-place use per channel is allowed. Longer blocks are not processed.
    void Process(const float* left, const float* right, float* out_left, float* out_right, std::size_t frames) {
        if(frames > MaxBlock)
            return;
        float feed_left[MaxBlock], feed_right[MaxBlock], wet_left[MaxBlock], wet_right[MaxBlock];
        for(std::size_t i = 0; i < frames; ++i) {
            feed_ = Toward(feed_, enabled_ ? 1.f : 0.f);
            feed_left[i] = left[i] * feed_;
            feed_right[i] = right[i] * feed_;
        }
        effect_.Process(StereoBlock{feed_left, feed_right, wet_left, wet_right, frames});
        for(std::size_t i = 0; i < frames; ++i) {
            mix_ = Toward(mix_, mix_target_);
            const float dry = 1.f - mix_;
            out_left[i] = left[i] * dry + wet_left[i] * mix_;
            out_right[i] = right[i] * dry + wet_right[i] * mix_;
        }
    }

  private:
    static constexpr float kStep = 1.f / 480.f; // 10 ms at 48 kHz

    static float Toward(float value, float target) {
        if(value < target)
            return value + kStep < target ? value + kStep : target;
        return value - kStep > target ? value - kStep : target;
    }

    Effect& effect_;
    bool enabled_ = true;
    float feed_ = 1.f, mix_ = 0.f, mix_target_ = 0.f;
};

} // namespace seed::audio
