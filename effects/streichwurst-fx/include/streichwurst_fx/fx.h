#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst FX component: separately callable string stage and global reverb stage
// (multi-bus contract STREICHWURST_FX_CONTRACT_VERSION, MODULAR_ARCHITECTURE.md).
//
//  * Prepare(config, storage) once outside the audio callback; storage is owned and
//    placed by the host (reverb lines). Reset() clears all state; ClearTails() empties
//    the delay line and reverb only.
//  * ProcessStrings(in, out_l, out_r, gain, n): mono string sum -> Animate forming ->
//    Ensemble common sum -> Phaser -> stereo tap correction. `in` may alias `out_l`
//    exactly; `gain` (optional, n values) receives the relative output gain word of the
//    ensemble state (SF-F06), which the host applies after solo mix and reverb.
//  * AnimateRegistrationOffset(): typed control output for the instrument's string
//    registration (native offset m), updated during ProcessStrings; the host hands it to
//    the instrument for the next block (control cadence = host block).
//  * ProcessReverb(in_l, in_r, wet_l, wet_r, n): wet-only global reverb of the later
//    mix; ReverbWet() is the smoothed mix amount the host uses for its dry/wet blend.
// No instrument ownership, no MIDI parser, no board or app code. Bounded, allocation-,
// lock- and I/O-free processing. Invalid blocks are rejected before touching buffers.
#include "ensemble.h"
#include "params.h"
#include "stages.h"
#include "version.h"
#include <cstddef>

namespace streichwurst_fx {

struct FxConfig {
    float sample_rate = 48000.f;
    std::size_t max_block = 48;
};

struct FxStorage {
    ReverbStorage reverb;
};

class StreichwurstFx {
  public:
    static constexpr float kMaxReverbWet = 0.5f; // own: wet amount 0..100 % -> 0..0.5

    bool Prepare(const FxConfig& config, FxStorage& storage) {
        if(config.max_block == 0 || !ensemble_.Prepare(config.sample_rate) ||
           !reverb_.Prepare(config.sample_rate, storage.reverb))
            return false;
        config_ = config;
        animate_.Prepare(config.sample_rate);
        phaser_.Prepare(config.sample_rate);
        wet_.Prepare(config.sample_rate, 0.02f);
        prepared_ = true;
        Apply();
        Reset();
        return true;
    }
    void Reset() {
        ensemble_.Reset();
        animate_.Reset();
        phaser_.Reset();
        reverb_.Clear();
        wet_.Snap();
        hop_ = 0;
    }
    void ClearTails() {
        ensemble_.ClearLine();
        reverb_.Clear();
    }
    void SetParameters(const FxParams& in) {
        params_ = Sanitize(in, params_);
        Apply();
    }
    const FxParams& Parameters() const { return params_; }

    bool ProcessStrings(const float* in, float* out_l, float* out_r, float* gain, std::size_t n) {
        if(!prepared_ || n > config_.max_block || (n && (!in || !out_l || !out_r || out_l == out_r)))
            return false;
        for(std::size_t i = 0; i < n; ++i) {
            const float formed = animate_.Process(in[i]);
            const EnsembleSum e = ensemble_.Process(formed);
            const float p = phaser_.Process(e.sum);
            out_l[i] = p - kStereoTapFactor * e.tap_l;
            out_r[i] = p - kStereoTapFactor * e.tap_r;
            if(gain)
                gain[i] = e.gain;
        }
        return true;
    }
    float AnimateRegistrationOffset() const { return animate_.RegistrationOffset(); }

    bool ProcessReverb(const float* in_l, const float* in_r, float* wet_l, float* wet_r, std::size_t n) {
        if(!prepared_ || n > config_.max_block || (n && (!in_l || !in_r || !wet_l || !wet_r || wet_l == wet_r)))
            return false;
        for(std::size_t i = 0; i < n; ++i) {
            if(hop_ == 0)
                wet_.Step();
            if(++hop_ == kControlHop)
                hop_ = 0;
            reverb_.Process(in_l[i], in_r[i], wet_l[i], wet_r[i]);
        }
        return true;
    }
    float ReverbWet() const { return wet_.value; }

    const Ensemble& GetEnsemble() const { return ensemble_; }
    const Reverb& GetReverb() const { return reverb_; }

  private:
    void Apply() {
        animate_.Set(params_.animate_amount, params_.animate_rate_hz);
        ensemble_.Configure(params_.ensemble_on, params_.ensemble_mode);
        phaser_.Set(params_.phaser_amount, params_.phaser_rate_hz);
        reverb_.SetDecay(params_.reverb_decay);
        wet_.target = params_.reverb_wet * kMaxReverbWet;
    }

    bool prepared_ = false;
    FxConfig config_;
    FxParams params_;
    Animate animate_;
    Ensemble ensemble_;
    Phaser phaser_;
    Reverb reverb_;
    Smoothed wet_;
    int hop_ = 0;
};

} // namespace streichwurst_fx
