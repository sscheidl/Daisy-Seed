#pragma once
// MIT. Board-independent Toy Piano -> Tape FX routing for the toy-piano-tape product.
// The piano renders mono; the same signal feeds both Tape FX inputs, so hiss and the
// per-channel dropout gains are the only stereo differences.
#include "toy_piano.h"
#include "../../../effects/tape-fx/src/tape_fx.h"
#include "../../../effects/tape-fx/src/tape_mixer.h"
#include "../../../effects/tape-fx/src/tape_panel.h"
#include <array>

namespace toy_piano {
// Every pot controls the tape, so the piano times are fixed: attack 2 ms and decay
// LogTime(0.83) = 1.5 s, long enough to hear wow and flutter on held notes.
constexpr float kTapeDecayBase = 0.83f;

class TapeHost {
  public:
    bool Prepare() {
        panel_.Reset();
        mixer_.Reset(panel_.Mix(), panel_.Bypass());
        effect_.SetParameters(panel_.Apply({}));
        return effect_.Prepare({48000, kBlockSize}, storage_);
    }
    // Control rate (once per block): VR1..VR8 and the two switches.
    void ReadControls(const std::array<float, 8>& pots, bool sw17_pressed, int sw18_position) {
        if(panel_.Update(pots, sw17_pressed, sw18_position))
            effect_.SetParameters(panel_.Apply({}));
        mixer_.SetMix(panel_.Mix());
        mixer_.SetBypass(panel_.Bypass());
    }
    bool Process(const float* mono, float* left, float* right, std::size_t frames) {
        if(frames != kBlockSize || !mono || !left || !right || left == right)
            return false;
        if(!effect_.Process({mono, mono, left, right, frames}))
            for(std::size_t i = 0; i < frames; ++i)
                left[i] = right[i] = 0.f;
        mixer_.Process(mono, mono, left, right, frames);
        return true;
    }
    const seed::fx::TapePanel& Panel() const { return panel_; }
    const seed::fx::TapeFx& Effect() const { return effect_; }
    float MixNow() const { return mixer_.Current(); }
  private:
    seed::fx::TapeFx effect_;
    seed::fx::TapeFx::Storage storage_;
    seed::fx::TapePanel panel_;
    seed::fx::TapeMixer mixer_;
};
} // namespace toy_piano
