#pragma once
// SPDX-License-Identifier: MIT
// Adapter only. The MIT-licensed Torus DSP remains at its single provenance path.
#include "../../../common/audio/fx_contract.h"
#include "../../../instruments/physical-modeling/torus/dsp/fx/reverb.h"
#include <array>
#include <cmath>

namespace seed::fx {
struct TorusReverbParameters {
    float decay = .5f, damping = .5f, diffusion = .625f / .9f, input_gain = .4f;
};
struct TorusReverbStorage {
    std::array<uint16_t, 32768> words;
};
class TorusReverb {
  public:
    using Parameters = TorusReverbParameters;
    using Storage = TorusReverbStorage;
    bool Prepare(audio::Config config, Storage& storage) {
        ready_ = false;
        if(config.sample_rate != 48000 || !config.max_block)
            return false; // The original LFO/delay tuning is fixed at 48 kHz.
        config_ = config;
        core_.Init(storage.words.data());
        ready_ = true;
        SetParameters(parameters_);
        return true;
    }
    void SetParameters(Parameters p) {
        p.decay = Unit(p.decay, .5f);
        p.damping = Unit(p.damping, .5f);
        p.diffusion = Unit(p.diffusion, .625f / .9f);
        p.input_gain = Unit(p.input_gain, .4f);
        parameters_ = p;
        // Same expressions/order as the accepted Torus instrument.
        core_.set_time(.35f + .63f * p.decay);
        core_.set_lp(.05f + .9f * (1.f - p.damping));
        core_.set_diffusion(.9f * p.diffusion);
        core_.set_input_gain(.5f * p.input_gain);
        core_.set_amount(1.f);
    }
    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block))
            return false;
        for(std::size_t i = 0; i < block.frames; ++i) {
            block.out_left[i] = block.left[i];
            block.out_right[i] = block.right[i];
        }
        if(block.frames)
            core_.Process(block.out_left, block.out_right, block.frames);
        return true;
    }
  private:
    static float Unit(float x, float fallback) {
        return std::isfinite(x) ? std::max(0.f, std::min(1.f, x)) : fallback;
    }
    audio::Config config_{};
    Parameters parameters_{};
    torus::Reverb core_;
    bool ready_ = false;
};
} // namespace seed::fx
