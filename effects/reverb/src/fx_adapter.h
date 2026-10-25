#pragma once
// SPDX-License-Identifier: MIT
#include "../../../common/audio/fx_contract.h"
#include "wet_path.hpp"

namespace seed::fx {
// Full Ambient parameters remain available. Routing controls are owned by the
// host: mix/output gain are unity, bypass is disabled and stereo input is explicit.
class AmbientReverb {
  public:
    using Parameters = ambient::Parameters;
    using Storage = ambient::Memory;
    bool Prepare(audio::Config config, Storage& storage) {
        ready_ = false;
        if(config.sample_rate != 48000 || !config.max_block)
            return false;
        config_ = config;
        path_.SetParameters(WetOnly(requested_));
        ready_ = path_.Prepare(float(config.sample_rate), storage);
        if(ready_) path_.ForceWetOnly();
        until_control_ = 0;
        return ready_;
    }
    // Cheap request copy. Libm/filter work is rate-limited inside Process to 50 Hz.
    void SetParameters(const Parameters& parameters) {
        requested_ = parameters;
        dirty_ = true;
    }
    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block))
            return false;
        for(std::size_t i = 0; i < block.frames; ++i) {
            if(until_control_ == 0) {
                if(dirty_) {
                    path_.SetParameters(WetOnly(requested_));
                    path_.ForceWetOnly();
                    dirty_ = false;
                }
                path_.ServiceFilter();
                until_control_ = config_.sample_rate / 50;
            }
            --until_control_;
            const auto out = path_.Process({block.left[i], block.right[i]}, true);
            block.out_left[i] = out.left;
            block.out_right[i] = out.right;
        }
        return true;
    }
  private:
    static Parameters WetOnly(Parameters p) {
        p.mix = 1.f;
        p.outputGain = 1.f;
        p.bypass = false;
        p.trails = true;
        return p;
    }
    audio::Config config_{};
    Parameters requested_{};
    ambient::WetPath path_;
    unsigned until_control_ = 0;
    bool ready_ = false, dirty_ = true;
};
} // namespace seed::fx
