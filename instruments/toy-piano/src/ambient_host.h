#pragma once
// MIT. Board-independent parameter mapping and routing for the Toy Piano FX product.
#include "toy_piano.h"
#include "../../../effects/reverb/src/fx_adapter.h"
#include <array>
#include <cmath>

namespace toy_piano {
class AmbientHost {
  public:
    bool Prepare() {
        count_ = 0;
        selected_ = candidate_ = 0;
        stable_ = 0;
        pitch_ = .5f; decay_ = .5f; mix_ = 0; texture_ = .5f;
        pots_ = {}; knobs_ = {};
        dry_.Set(1.f, 0); wet_.Set(0.f, 0); send_.Set(1.f, 0);
        return effect_.Prepare({48000, kBlockSize}, memory_);
    }
    void ReadControls(const std::array<float, 8>& values, bool bank, int position) {
        for(unsigned i = 0; i < 8; ++i) knobs_[i] = pots_[i].Process(values[i]);
        // Center detent in software for exact concert pitch despite ADC noise.
        pitch_ = std::fabs(knobs_[0] - .5f) < .005f ? .5f : knobs_[0];
        const unsigned request = (bank ? 3u : 0u) + (position == 1 ? 0u : position == 2 ? 2u : 1u);
        if(request != candidate_) { candidate_ = request; stable_ = 1; }
        else if(stable_ < 15) ++stable_;
        if(stable_ == 15) selected_ = candidate_;
    }
    float PitchBase() const { return pitch_; }
    float DecayBase() const { return knobs_[1]; }
    float ReverbDecayBase() const { return knobs_[2]; }
    float MixBase() const { return knobs_[5]; }
    float XBase() const { return knobs_[6]; }
    unsigned SelectedMode() const { return selected_; }
    // Normalized values after the matrix. This just updates requests; expensive
    // mapping and the adapter's native coefficients run at no more than 50 Hz.
    void SetModulated(float decay, float mix, float texture) {
        decay_ = decay; mix_ = mix; texture_ = texture;
    }
    bool Process(const float* mono, float* left, float* right, std::size_t frames) {
        if(frames != kBlockSize || !mono || !left || !right || left == right) return false;
        if(count_ == 0) {
            ambient::Parameters p;
            constexpr ambient::Mode modes[5] = {ambient::Mode::Cloud, ambient::Mode::Halo,
                ambient::Mode::Scatter, ambient::Mode::Abyss, ambient::Mode::Bloom};
            p.mode = modes[std::min(selected_, 4u)];
            p.decaySeconds = std::pow(45.f, decay_);
            p.toneHz = 400.f * std::pow(35.f, knobs_[3]);
            p.modDepthMs = 3.f * knobs_[4];
            // Without a Sustain button, Cloud X scales its vibrato contribution.
            if(selected_ == 0) p.modDepthMs *= .5f + .5f * texture_;
            p.texture = texture_;
            p.riseSeconds = .05f * std::pow(80.f, texture_);
            p.stretch = knobs_[7];
            effect_.SetParameters(p);
            count_ = 20;
        }
        --count_;
        const bool dry = selected_ == 5;
        const float mix = dry ? 0.f : mix_;
        const float dry_gain = mix == 1.f ? 0.f : std::cos(mix * ambient::kPi * .5f);
        const float wet_gain = mix == 0.f ? 0.f : std::sin(mix * ambient::kPi * .5f);
        dry_.Set(dry_gain, 1920);
        wet_.Set(wet_gain, 1920);
        send_.Set(dry ? 0.f : 1.f, 1920);
        for(std::size_t i = 0; i < frames; ++i) {
            send_buffer_[i] = mono[i] * send_.Next();
        }
        if(!effect_.Process({send_buffer_.data(), send_buffer_.data(), left, right, frames})) return false;
        for(std::size_t i = 0; i < frames; ++i) {
            const float a = dry_.Next(), b = wet_.Next();
            left[i] = Protect(mono[i] * a + left[i] * b);
            right[i] = Protect(mono[i] * a + right[i] * b);
        }
        return true;
    }
    static float Protect(float x) {
        if(!std::isfinite(x)) return 0;
        const float a = std::fabs(x);
        if(a <= .9f) return x;
        const float excess = a - .9f;
        return std::copysign(.9f + .08f * excess / (.08f + excess), x);
    }
  private:
    seed::fx::AmbientReverb effect_;
    seed::fx::AmbientReverb::Storage memory_;
    std::array<PotFilter, 8> pots_{};
    std::array<float, 8> knobs_{};
    std::array<float, kBlockSize> send_buffer_{};
    ambient::Ramp dry_{1.f, 1.f, 0.f, 0}, wet_{}, send_{1.f, 1.f, 0.f, 0};
    float pitch_ = .5f, decay_ = .5f, mix_ = 0, texture_ = .5f;
    unsigned count_ = 0, selected_ = 0, candidate_ = 0, stable_ = 0;
};
} // namespace toy_piano
