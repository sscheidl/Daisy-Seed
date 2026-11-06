#pragma once
// SPDX-License-Identifier: MIT
// Own control laws and constants of the Grain Cloud core. Control-rate code only: nothing
// here runs per sample (rule R3), except Ramp::Next. fastmath.h gives the same bits with
// every C library.
#include "../../../../common/dsp/fastmath.h"
#include <cstdint>

namespace grain_cloud {

// B4: density lives in octaves of rate, so equal knob steps and equal modulation depths
// are equal ratios at every base value.
constexpr float kDensityMinHz = 0.5f, kDensityOctaves = 11.f; // 0.5 .. 1024 grains/s
// Grain length in output frames, the same warped domain.
constexpr float kSizeMinSeconds = 0.002f, kSizeOctaves = 10.f; // 2 ms .. 2.05 s
constexpr float kPitchSpanSemitones = 48.f;                    // Pitch 0..1 = -24 .. +24
constexpr float kMaxPitchRatio = 16.f;
constexpr float kScanMaxPerSecond = 0.25f; // of the sample span, at Scan 0 and 1

// Envelope times in seconds, each parameter spanning ten octaves of time.
constexpr float kMinAttackSeconds = 0.004f, kAttackOctaves = 10.f;
constexpr float kMinDecaySeconds = 0.01f, kDecayOctaves = 10.f;
constexpr float kFastReleaseSeconds = 0.005f; // stealing and panic

// R6: gain = 1 / sqrt(1 + kCountLaw * (n - 1)). One grain keeps full level; a dense cloud of
// unrelated grains settles near a constant RMS. Own value.
constexpr float kCountLaw = 0.5f;
constexpr float kCountSmoothingSeconds = 0.03f;
constexpr float kVoiceHeadroom = 0.35f; // R5: eight full voices stay near the clip point

// B11: a stolen grain reaches the end of its window within this many frames.
constexpr uint32_t kStealFrames = 192; // 4 ms at 48 kHz
constexpr uint32_t kMaxStealsPerBlock = 4;
constexpr uint32_t kMaxBirthsPerBlock = 16; // spawn bound across all voices
constexpr uint32_t kMaxEventsPerBlock = 16;
constexpr uint32_t kPreferredSlots = 4;     // fair share floor per voice

inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

inline float DensityHz(float value) { return kDensityMinHz * seed::dsp::Exp2(Clamp01(value) * kDensityOctaves); }
inline float SizeSeconds(float value) { return kSizeMinSeconds * seed::dsp::Exp2(Clamp01(value) * kSizeOctaves); }
inline float PitchSemitones(float value) { return (Clamp01(value) - 0.5f) * kPitchSpanSemitones; }
inline float PitchRatio(float semitones) {
    const float ratio = seed::dsp::Exp2(semitones * (1.f / 12.f));
    return ratio > kMaxPitchRatio ? kMaxPitchRatio : ratio;
}
inline float AttackSeconds(float value) { return kMinAttackSeconds * seed::dsp::Exp2(Clamp01(value) * kAttackOctaves); }
inline float DecaySeconds(float value) { return kMinDecaySeconds * seed::dsp::Exp2(Clamp01(value) * kDecayOctaves); }

// R6: smoothed grain count to voice gain.
inline float CountGain(float count) {
    const float extra = count > 1.f ? count - 1.f : 0.f;
    return seed::dsp::Exp2(-0.5f * seed::dsp::Log2(1.f + kCountLaw * extra));
}

// Linear ramp to the tick's target (R4). One add per frame while it runs.
class Ramp {
  public:
    void Reset(float value) {
        value_ = target_ = value;
        step_ = 0.f;
        left_ = 0;
    }
    void Set(float target, uint32_t frames) {
        target_ = target;
        left_ = frames;
        if(frames)
            step_ = (target - value_) / float(frames);
        else
            value_ = target;
    }
    float Next() {
        if(left_) {
            value_ += step_;
            if(--left_ == 0)
                value_ = target_;
        }
        return value_;
    }
    float Value() const { return value_; }
    float Target() const { return target_; }
    bool Done() const { return left_ == 0; }

  private:
    float value_ = 0.f, target_ = 0.f, step_ = 0.f;
    uint32_t left_ = 0;
};

} // namespace grain_cloud
