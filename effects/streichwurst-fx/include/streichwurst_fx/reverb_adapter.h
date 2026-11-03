#pragma once
// SPDX-License-Identifier: MIT
// The Streichwurst reverb as a wet-only stereo stage of the shared FX contract 1.0
// (common/audio/fx_contract.h). Products can put it into any contract slot, and the
// Streichwurst product's global slot accepts any other contract stage instead (for example
// a delay) without changes to the instrument or the string stage.
#include "../../../../common/audio/fx_contract.h"
#include "stages.h"

namespace streichwurst_fx {

class ReverbFx {
  public:
    using Storage = ReverbStorage;
    struct Parameters {
        float decay = 0.5f; // 0..1
    };
    bool Prepare(const seed::audio::Config& config, Storage& storage) {
        config_ = config;
        ready_ = reverb_.Prepare(float(config.sample_rate), storage);
        return ready_;
    }
    void SetParameters(const Parameters& p) {
        const float d = std::isfinite(p.decay) ? (p.decay < 0.f ? 0.f : (p.decay > 1.f ? 1.f : p.decay)) : 0.5f;
        reverb_.SetDecay(d);
    }
    bool Process(const seed::audio::StereoBlock& b) {
        if(!ready_ || !seed::audio::Valid(config_, b))
            return false;
        for(std::size_t i = 0; i < b.frames; ++i) {
            float l, r;
            reverb_.Process(b.left[i], b.right[i], l, r);
            b.out_left[i] = l;
            b.out_right[i] = r;
        }
        return true;
    }
    void Clear() { reverb_.Clear(); }

  private:
    seed::audio::Config config_;
    Reverb reverb_;
    bool ready_ = false;
};

} // namespace streichwurst_fx
