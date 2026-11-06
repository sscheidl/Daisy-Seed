#pragma once
// SPDX-License-Identifier: MIT
// Host side of Filter FX (FX contract: the host owns dry/wet and transitions). Dry/wet moves
// without a step; on/off is the Drive component's 10 ms crossfade (DriveMixer), behind this.
#include <cstddef>

namespace seed::fx {

class FilterDryWet {
  public:
    static constexpr float kFadeMs = 10.f; // for the whole range
    void Reset(float mix) { mix_ = target_ = mix; }
    void Set(float mix) { target_ = mix; }
    // wet = dry + mix * (wet - dry), in place on wet.
    void Process(const float* dry_l, const float* dry_r, float* wet_l, float* wet_r, std::size_t n) {
        const float step = 1.f / (kFadeMs * 48.f);
        for(std::size_t i = 0; i < n; ++i) {
            if(mix_ < target_)
                mix_ = mix_ + step > target_ ? target_ : mix_ + step;
            else if(mix_ > target_)
                mix_ = mix_ - step < target_ ? target_ : mix_ - step;
            wet_l[i] = dry_l[i] + mix_ * (wet_l[i] - dry_l[i]);
            wet_r[i] = dry_r[i] + mix_ * (wet_r[i] - dry_r[i]);
        }
    }
    float Mix() const { return mix_; }

  private:
    float mix_ = 1.f, target_ = 1.f;
};

} // namespace seed::fx
