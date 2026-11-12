#pragma once
// SPDX-License-Identifier: MIT
// Host side of a standalone wet-only effect under the FX contract 1.0: the feed fades over
// 10 ms when switched, so the tail rings on, and dry/wet glides over 10 ms. A mix of 0 gives
// the exact dry signal. Effects with their own tail policy keep their own host (stereo delay).
// Effects report a rejected block with `bool Process(StereoBlock)` returning false.
#include "fx_contract.h"
#include <cstddef>
#include <type_traits>
#include <utility>
#define SEED_WET_HOST_VERSION "0.2.0"

namespace seed::audio {

template <class Effect, std::size_t MaxBlock> class WetHost {
  public:
    // FX contract 1.0 lets an effect reject a block without touching any buffer. A void Process
    // could not tell the host, which would then mix unwritten wet buffers (R13), so it is refused.
    static_assert(std::is_same_v<decltype(std::declval<Effect&>().Process(std::declval<StereoBlock>())), bool>,
                  "WetHost effects must report rejection: bool Process(StereoBlock)");

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
    // If the effect rejects, return dry without reading its unwritten wet buffers (R12).
    void Process(const float* left, const float* right, float* out_left, float* out_right, std::size_t frames) {
        if(frames > MaxBlock)
            return;
        float feed_left[MaxBlock], feed_right[MaxBlock], wet_left[MaxBlock], wet_right[MaxBlock];
        for(std::size_t i = 0; i < frames; ++i) {
            feed_ = Toward(feed_, enabled_ ? 1.f : 0.f);
            feed_left[i] = left[i] * feed_;
            feed_right[i] = right[i] * feed_;
        }
        if(!effect_.Process(StereoBlock{feed_left, feed_right, wet_left, wet_right, frames})) {
            for(std::size_t i = 0; i < frames; ++i) {
                out_left[i] = left[i];
                out_right[i] = right[i];
            }
            return;
        }
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
