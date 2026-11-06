#pragma once
// SPDX-License-Identifier: MIT
// C7: the voice envelope, stepped once per control tick. Linear attack, exponential decay
// and release, plus a separate fast release for stealing and panic. The voice gain ramps
// between ticks (R4), so the reduced rate is not audible as steps.
#include "laws.h"
#include <cstdint>

namespace grain_cloud {

// Per-tick steps, recomputed only when an envelope parameter changes.
struct EnvelopeRates {
    float attack_step = 1.f;
    float decay_coeff = 0.f;
    float release_coeff = 0.f;
    float sustain = 1.f;
    uint32_t fast_ticks = 2;
};

// A stage of the given length falls by 60 dB: log2(1000) octaves of amplitude.
inline float CurveCoefficient(float seconds, float tick_seconds) {
    return seed::dsp::Exp2(-9.96578428f * tick_seconds / seconds);
}

inline EnvelopeRates MakeRates(float attack, float decay, float sustain, float release, float tick_seconds) {
    EnvelopeRates r;
    r.attack_step = tick_seconds / AttackSeconds(attack);
    if(r.attack_step > 1.f)
        r.attack_step = 1.f;
    r.decay_coeff = CurveCoefficient(DecaySeconds(decay), tick_seconds);
    r.release_coeff = CurveCoefficient(DecaySeconds(release), tick_seconds);
    r.sustain = Clamp01(sustain);
    const float ticks = kFastReleaseSeconds / tick_seconds;
    r.fast_ticks = ticks < 2.f ? 2u : uint32_t(ticks + 0.999f);
    return r;
}

class Envelope {
  public:
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release, FastRelease, Hold };
    static constexpr float kSilence = 1e-4f; // -80 dB: the rest is one tick of ramp

    void Reset() {
        level_ = 0.f;
        stage_ = Stage::Idle;
    }
    // From the current level, so a retrigger cannot step.
    void NoteOn() { stage_ = Stage::Attack; }
    void NoteOff() {
        if(stage_ != Stage::Idle && stage_ != Stage::FastRelease && stage_ != Stage::Hold)
            stage_ = Stage::Release;
    }
    void BeginFastRelease(const EnvelopeRates& rates) {
        if(stage_ == Stage::Idle || stage_ == Stage::FastRelease)
            return;
        fast_step_ = level_ / float(rates.fast_ticks);
        stage_ = Stage::FastRelease;
    }
    // A stolen note whose grains run out on their own: the level stays where it is.
    void BeginHold() {
        if(stage_ != Stage::Idle && stage_ != Stage::FastRelease)
            stage_ = Stage::Hold;
    }

    float Step(const EnvelopeRates& rates) {
        switch(stage_) {
            case Stage::Idle:
            case Stage::Hold: break;
            case Stage::Attack:
                level_ += rates.attack_step;
                if(level_ >= 1.f) {
                    level_ = 1.f;
                    stage_ = Stage::Decay;
                }
                break;
            case Stage::Decay:
                level_ = rates.sustain + (level_ - rates.sustain) * rates.decay_coeff;
                if(level_ - rates.sustain < kSilence) {
                    level_ = rates.sustain;
                    stage_ = Stage::Sustain;
                }
                break;
            case Stage::Sustain:
                level_ = rates.sustain; // the voice gain ramp absorbs a changed sustain
                break;
            case Stage::Release:
                level_ *= rates.release_coeff;
                if(level_ < kSilence)
                    Finish();
                break;
            case Stage::FastRelease:
                level_ -= fast_step_;
                if(level_ <= 1e-7f)
                    Finish();
                break;
        }
        return level_;
    }
    float Level() const { return level_; }
    Stage CurrentStage() const { return stage_; }
    bool Idle() const { return stage_ == Stage::Idle; }

  private:
    void Finish() {
        level_ = 0.f;
        stage_ = Stage::Idle;
    }
    float level_ = 0.f, fast_step_ = 0.f;
    Stage stage_ = Stage::Idle;
};

} // namespace grain_cloud
