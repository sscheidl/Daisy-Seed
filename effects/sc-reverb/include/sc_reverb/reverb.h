#pragma once
// SPDX-License-Identifier: MIT
// SC Reverb: the Sean Costello reverb of DaisySP-LGPL (ReverbSc, LGPL-2.1, see README.md),
// wet-only under the FX contract 1.0. This file only wraps it and is MIT; a build that links
// reverbsc.cpp carries the LGPL duties. The host provides the memory: the reverb is
// constructed in it by Prepare(), so it may be SDRAM that only works after hardware start-up.
#include "../../../../common/audio/fx_contract.h"
#include <Effects/reverbsc.h> // DaisySP-LGPL
#include <cstddef>
#include <new>

namespace seed::fx {
namespace screverb {
inline constexpr unsigned kSampleRate = 48000;
inline constexpr float kMinLpHz = 100.f, kMaxLpHz = 20000.f;

struct Parameters {
    float feedback = 0.85f; // 0..1, decay
    float lp_hz = 12000.f;  // 100..20000, damping in the delay lines
};
} // namespace screverb

class ScReverb {
  public:
    using Parameters = screverb::Parameters;
    // Raw memory for the reverb, about 396 KB. The host places it (SDRAM).
    struct Storage {
        alignas(daisysp::ReverbSc) unsigned char reverb[sizeof(daisysp::ReverbSc)];
    };

    // Startup only: clears the delay lines.
    bool Prepare(audio::Config config, Storage& storage) {
        if(config.sample_rate != screverb::kSampleRate || !config.max_block)
            return false;
        reverb_ = new(storage.reverb) daisysp::ReverbSc;
        if(reverb_->Init(float(config.sample_rate)) != 0) {
            reverb_ = nullptr;
            return false;
        }
        config_ = config;
        requested_ = Parameters();
        Apply();
        return true;
    }

    void SetParameters(const Parameters& p) {
        requested_.feedback = Limit(p.feedback, 0.f, 1.f, requested_.feedback);
        requested_.lp_hz = Limit(p.lp_hz, screverb::kMinLpHz, screverb::kMaxLpHz, requested_.lp_hz);
        if(reverb_)
            Apply();
    }
    const Parameters& Requested() const { return requested_; }

    void Process(const audio::StereoBlock& block) {
        if(!reverb_ || !audio::Valid(config_, block))
            return;
        for(std::size_t i = 0; i < block.frames; ++i) {
            const float left = block.left[i], right = block.right[i];
            reverb_->Process(left, right, &block.out_left[i], &block.out_right[i]);
        }
    }

  private:
    // NaN keeps the previous value.
    static float Limit(float value, float low, float high, float previous) {
        if(value != value)
            return previous;
        return value < low ? low : (value > high ? high : value);
    }
    void Apply() {
        reverb_->SetFeedback(requested_.feedback);
        reverb_->SetLpFreq(requested_.lp_hz);
    }

    audio::Config config_;
    Parameters requested_;
    daisysp::ReverbSc* reverb_ = nullptr;
};

} // namespace seed::fx
