#pragma once
// SPDX-License-Identifier: MIT
// Host side of Drive (FX contract: the host owns transitions): on/off as a 10 ms linear
// crossfade between the dry input and the wet drive output (ARBEITSAUFTRAG 4.7).
#include <cstddef>

namespace seed::fx {

class DriveMixer {
  public:
    static constexpr float kFadeMs = 10.f;
    void Reset(bool enabled) { mix_ = enabled ? 1.f : 0.f; }
    void SetEnabled(bool enabled) { target_ = enabled ? 1.f : 0.f; }
    bool Active() const { return mix_ > 0.f || target_ > 0.f; } // the wet path still matters
    // out = dry + mix * (wet - dry); in place on `out` is allowed (out may be `wet`).
    void Process(const float* dry_l, const float* dry_r, const float* wet_l, const float* wet_r, float* out_l,
                 float* out_r, std::size_t n) {
        const float step = 1.f / (kFadeMs * 48.f);
        for(std::size_t i = 0; i < n; ++i) {
            if(mix_ < target_)
                mix_ = mix_ + step > target_ ? target_ : mix_ + step;
            else if(mix_ > target_)
                mix_ = mix_ - step < target_ ? target_ : mix_ - step;
            out_l[i] = dry_l[i] + mix_ * (wet_l[i] - dry_l[i]);
            out_r[i] = dry_r[i] + mix_ * (wet_r[i] - dry_r[i]);
        }
    }
    float Mix() const { return mix_; }

  private:
    float mix_ = 1.f, target_ = 1.f;
};

} // namespace seed::fx
