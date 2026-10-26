#pragma once
// SPDX-License-Identifier: MIT
// Host side of the Tape FX (the FX contract leaves dry/wet to the host): TapeVibe's
// linear dry/wet with its +-1.2 clamp, a click-free bypass and codec protection.
#include <cmath>
#include <cstddef>

namespace seed::fx {
class TapeMixer {
  public:
    // A full 0 -> 1 mix change takes kSlewSeconds; smaller changes are proportionally shorter.
    static constexpr float kSlewSeconds = 0.010f;
    void Reset(float mix, bool bypass, float sample_rate = 48000.0f) {
        step_ = 1.0f / (kSlewSeconds * sample_rate);
        SetMix(mix);
        SetBypass(bypass);
        current_ = Target();
    }
    void SetMix(float mix) { mix_ = std::isfinite(mix) ? (mix < 0.0f ? 0.0f : (mix > 1.0f ? 1.0f : mix)) : 0.0f; }
    void SetBypass(bool bypass) { bypass_ = bypass; }
    float Target() const { return bypass_ ? 0.0f : mix_; }
    float Current() const { return current_; }
    // io holds the wet signal on entry and the protected output on return.
    // Bypass glides the mix to exactly 0, so the output is then exactly the dry input.
    void Process(const float* dry_left, const float* dry_right, float* io_left, float* io_right,
                 std::size_t frames) {
        const float target = Target();
        for(std::size_t i = 0; i < frames; ++i) {
            const float d = target - current_;
            current_ = d > step_ ? current_ + step_ : (d < -step_ ? current_ - step_ : target);
            io_left[i] = Output(Mix(dry_left[i], io_left[i], current_));
            io_right[i] = Output(Mix(dry_right[i], io_right[i], current_));
        }
    }
    // TapeVibe: clampAudio(dry + (wet - dry) * mix).
    static float Mix(float dry, float wet, float mix) {
        if(!std::isfinite(wet))
            wet = 0.0f;
        const float x = dry + (wet - dry) * mix;
        return x < -1.2f ? -1.2f : (x > 1.2f ? 1.2f : x);
    }
    // Transparent below 0.95, soft knee above (as the Ambient Reverb firmware), finite.
    static float Output(float x) {
        if(!std::isfinite(x))
            return 0.0f;
        const float a = std::fabs(x);
        if(a <= 0.95f)
            return x;
        const float e = a - 0.95f;
        return std::copysign(0.95f + 0.05f * e / (0.05f + e), x);
    }
  private:
    float mix_ = 0.4f, current_ = 0.4f, step_ = 1.0f / 480.0f;
    bool bypass_ = false;
};
} // namespace seed::fx
