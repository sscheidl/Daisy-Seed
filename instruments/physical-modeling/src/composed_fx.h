#pragma once
// Compile-time effect selection. This file owns instrument-to-FX parameter mapping.
#include "parameter_manager.h"
#if PHYSICAL_MODELING_AMBIENT
#include "../../../effects/reverb/src/fx_adapter.h"
#else
#include "../../../effects/torus-reverb/src/torus_reverb.h"
#endif

namespace physical_modeling {
class ComposedReverb {
  public:
#if PHYSICAL_MODELING_AMBIENT
    using Effect = seed::fx::AmbientReverb;
#else
    using Effect = seed::fx::TorusReverb;
#endif
    using Storage = Effect::Storage;
    bool Prepare(Storage& storage) {
        until_control_ = 0;
        return effect_.Prepare({48000, kQuantum}, storage);
    }
    void SetParameters(const ParameterManager& p) {
#if PHYSICAL_MODELING_AMBIENT
        decay_ = p.Get(Decay);
        damping_ = p.Get(ReverbDamping);
        diffusion_ = p.Get(Diffusion);
        input_gain_ = .5f * p.Get(InputGain);
#else
        effect_.SetParameters({p.Get(Decay), p.Get(ReverbDamping),
                               p.Get(Diffusion), p.Get(InputGain)});
#endif
    }
    void Process(float* left, float* right, std::size_t frames) {
#if PHYSICAL_MODELING_AMBIENT
        // Pilot mapping: retain existing instrument pots/CCs. This is not the full
        // standalone Ambient panel. Libm runs at 50 Hz, never per DSP quantum.
        if(until_control_ == 0) {
            ambient::Parameters p;
            p.decaySeconds = std::pow(45.f, decay_);
            p.toneHz = 400.f * std::pow(35.f, 1.f - damping_);
            p.diffusion = diffusion_;
            effect_.SetParameters(p);
            until_control_ = 960;
        }
        until_control_ = until_control_ > frames ? until_control_ - unsigned(frames) : 0;
        for(std::size_t i = 0; i < frames; ++i) {
            left[i] *= input_gain_;
            right[i] *= input_gain_;
        }
#endif
        effect_.Process({left, right, left, right, frames});
    }
  private:
    Effect effect_;
    unsigned until_control_ = 0;
#if PHYSICAL_MODELING_AMBIENT
    float decay_ = .5f, damping_ = .5f, diffusion_ = .625f / .9f, input_gain_ = .2f;
#endif
};
} // namespace physical_modeling
