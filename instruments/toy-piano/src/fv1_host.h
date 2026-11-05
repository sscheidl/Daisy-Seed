#pragma once
// MIT. Board-independent Toy Piano -> FV-1 interpreter routing for the toy-piano-fv1 product
// (research / feasibility study). The piano renders mono; the same signal feeds both FX inputs.
// The fv1-vm component runs the program at 32 kHz behind its 48 kHz adapter (fv1vm/fv1_fx48.h).
// Mapping owned here (0.3.0, piano first): VR1/VR2 stay with the piano (main.cpp), VR3..VR5 =
// POT0..POT2, VR6 = dry/wet, SW17 = bypass (exact dry piano); VR7/VR8 unused. The program comes
// from MIDI (Program Change or channel 16 CC22/CC23, see effects/fv1-vm/src/fv1_firmware.h).
#include "toy_piano.h"
#include "fv1vm/fv1_fx48.h"
#include "fv1_firmware.h"
#include <array>

namespace toy_piano {
constexpr unsigned kFv1FirstPot = 2; // VR3
constexpr unsigned kFv1MixPot = 5;   // VR6

class Fv1Host {
  public:
    // Startup only. A table with count 0 selects the component's own demo programs.
    bool Prepare(fv1_fw::ProgramTable table) {
        bank_.Init(table);
        if(!fx_.Prepare({48000, kBlockSize}, storage_)) return false;
        Select(0);
        mix_ = target_mix_ = 1.f;
        return true;
    }
    // Control rate (once per block), audio thread: VR3..VR5, VR6 and SW17.
    void ReadControls(const std::array<float, 8>& pots, bool bypass) {
        seed::fx::fv1::Parameters p;
        for(unsigned i = 0; i < 3; ++i) p.pot[i] = pots[kFv1FirstPot + i];
        fx_.SetParameters(p);
        const float mix = pots[kFv1MixPot];
        target_mix_ = mix < 0.f ? 0.f : mix > 1.f ? 1.f : mix;
        bypass_ = bypass;
    }
    // Audio thread: loads the program and clears the delay RAM; the block is muted (dry in bypass).
    void Select(unsigned index) {
        program_ = index % bank_.Count();
        fx_.LoadProgram(bank_.Words(program_));
        fx_.Reset();
        muted_ = true;
    }
    bool Process(const float* mono, float* left, float* right, std::size_t frames) {
        if(frames != kBlockSize || !mono || !left || !right || left == right) return false;
        // The interpreter keeps running in bypass, so its state continues when bypass ends.
        const bool wet = !muted_ && fx_.Process({mono, mono, left, right, frames});
        muted_ = false;
        if(bypass_) {
            for(std::size_t i = 0; i < frames; ++i) left[i] = right[i] = mono[i];
            mix_ = target_mix_;
            return true;
        }
        if(!wet) {
            for(std::size_t i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
            return true;
        }
        // Dry/wet with a per-block linear ramp; the output is limited to +-1.
        const float step = (target_mix_ - mix_) / float(frames);
        for(std::size_t i = 0; i < frames; ++i) {
            mix_ += step;
            const float dry = mono[i] * (1.f - mix_);
            left[i] = Limit(dry + left[i] * mix_);
            right[i] = Limit(dry + right[i] * mix_);
        }
        mix_ = target_mix_;
        return true;
    }
    unsigned Program() const { return program_; }
    unsigned Count() const { return bank_.Count(); }
    bool LocalPrograms() const { return bank_.Local(); }
    bool Bypass() const { return bypass_; }
  private:
    static float Limit(float x) { return x > 1.f ? 1.f : x < -1.f ? -1.f : x; }
    seed::fx::fv1::Fx48<float> fx_;
    seed::fx::fv1::Storage<float> storage_;
    fv1_fw::ProgramBank bank_;
    unsigned program_ = 0;
    float mix_ = 1.f, target_mix_ = 1.f;
    bool muted_ = false, bypass_ = false;
};
} // namespace toy_piano
