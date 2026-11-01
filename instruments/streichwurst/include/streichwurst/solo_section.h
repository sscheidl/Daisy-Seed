#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst solo section: eight own voice objects (the original has eight pool objects,
// SF-A04 field map; its wave forming, envelope modes and stealing rule are OPEN). Every
// algorithm here is an own, documented replacement (docs/FIDELITY.md):
//  * oscillator: PolyBLEP square/saw/pulse morph by Tone plus two one-pole low-passes;
//  * envelope: attack -> decay -> sustain, release from any state, retrigger from the
//    current level; sustain modes decay / hold / percussive;
//  * tremolo: one shared LFO, g = 1 - d/2 + (d/2) sin(2 pi f t);
//  * allocation: same channel/note -> lowest free index -> lowest releasing level ->
//    lowest level; ties: oldest start, then lowest index. Phase and envelope stay
//    continuous when a voice is stolen.
#include "params.h"
#include <cmath>
#include <cstdint>

namespace streichwurst {

constexpr int kSoloVoices = 8;

struct SoloVoice {
    bool active = false, gate = false, sustained = false;
    uint8_t channel = 0, note = 0;
    enum Stage : uint8_t { Attack, Decay, Sustain, Release } stage = Release;
    float env = 0.f, velocity = 1.f;
    float phase = 0.f, inc = 0.f;
    float lp1 = 0.f, lp2 = 0.f, lp_a = 1.f;
    uint64_t start = 0;
};

class SoloSection {
  public:
    static constexpr float kAttackDone = 0.999f, kSilent = 1e-5f, kDecayTau = 0.08f;

    bool Prepare(float sample_rate) {
        if(!(sample_rate >= 8000.f && sample_rate <= 192000.f))
            return false;
        fs_ = sample_rate;
        Reset();
        return true;
    }
    void Reset() {
        for(auto& v : voices_)
            v = SoloVoice{};
        tremolo_phase_ = 0.f;
        clock_ = 0;
        pitch_current_ = pitch_target_;
        pitch_factor_ = std::exp2(pitch_current_ / 12.f);
        Update();
    }

    void SetParameters(const InstrumentParams& p) {
        params_ = p;
        Update();
    }
    void SetPitch(float semitones) { pitch_target_ = semitones; }

    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        SoloVoice& v = voices_[Allocate(channel, note)];
        v.active = true;
        v.gate = true;
        v.sustained = false;
        v.channel = channel;
        v.note = note;
        v.stage = SoloVoice::Attack; // from the current level, no reset
        v.velocity = (1.f - params_.solo_velocity) + params_.solo_velocity * float(velocity) / 127.f;
        v.start = ++clock_;
        SetFrequency(v);
    }
    // sustain: pedal down, the voice keeps its gate until the pedal is released.
    void NoteOff(uint8_t channel, uint8_t note, bool sustain) {
        for(auto& v : voices_)
            if(v.active && v.gate && v.channel == channel && v.note == note) {
                if(sustain) {
                    v.sustained = true;
                } else {
                    v.gate = false;
                    v.stage = SoloVoice::Release;
                }
            }
    }
    void PedalUp(uint8_t channel) {
        for(auto& v : voices_)
            if(v.active && v.gate && v.sustained && v.channel == channel) {
                v.gate = false;
                v.sustained = false;
                v.stage = SoloVoice::Release;
            }
    }
    void ReleaseAll() {
        for(auto& v : voices_)
            if(v.active) {
                v.gate = false;
                v.sustained = false;
                v.stage = SoloVoice::Release;
            }
    }
    void Silence() {
        for(auto& v : voices_)
            v = SoloVoice{};
    }

    float Process() {
        if(pitch_current_ != pitch_target_) {
            pitch_current_ += (pitch_target_ - pitch_current_) * pitch_alpha_;
            if(std::fabs(pitch_target_ - pitch_current_) < 1e-4f)
                pitch_current_ = pitch_target_;
            pitch_factor_ = std::exp2(pitch_current_ / 12.f);
            for(auto& v : voices_)
                if(v.active)
                    SetFrequency(v);
        }
        tremolo_phase_ += tremolo_inc_;
        if(tremolo_phase_ >= 1.f)
            tremolo_phase_ -= 1.f;
        const float d = params_.solo_tremolo_depth;
        const float trem = 1.f - 0.5f * d + 0.5f * d * std::sin(6.28318530718f * tremolo_phase_);
        float sum = 0.f;
        for(auto& v : voices_) {
            if(!v.active)
                continue;
            Envelope(v);
            if(!v.active)
                continue;
            sum += Oscillator(v) * v.env * v.velocity;
        }
        return sum * trem;
    }

    // Tests and diagnostics.
    const SoloVoice& Voice(int i) const { return voices_[i]; }
    int ActiveVoices() const {
        int n = 0;
        for(const auto& v : voices_)
            n += v.active;
        return n;
    }
    static float AttackTau(float u) { return 0.003f * std::exp2(11.f * u); }
    static float ReleaseTau(float u) { return 0.020f * std::exp2(9.f * u); }

  private:
    int Allocate(uint8_t channel, uint8_t note) const {
        for(int i = 0; i < kSoloVoices; ++i)
            if(voices_[i].active && voices_[i].channel == channel && voices_[i].note == note)
                return i;
        for(int i = 0; i < kSoloVoices; ++i)
            if(!voices_[i].active)
                return i;
        int best = -1;
        for(int pass = 0; pass < 2 && best < 0; ++pass)
            for(int i = 0; i < kSoloVoices; ++i) {
                const SoloVoice& v = voices_[i];
                if(pass == 0 && v.gate)
                    continue; // first only voices already releasing
                if(best < 0 || v.env < voices_[best].env ||
                   (v.env == voices_[best].env && v.start < voices_[best].start))
                    best = i;
            }
        return best;
    }
    void SetFrequency(SoloVoice& v) {
        float f = NoteFrequency(float(v.note)) * pitch_factor_;
        const float limit = 0.45f * fs_;
        if(f > limit)
            f = limit;
        v.inc = f / fs_;
        // Tone low-pass with own key tracking; coefficients only on pitch/tone changes.
        float fc = cutoff_base_ * std::sqrt(f / 440.f);
        if(fc > 0.45f * fs_)
            fc = 0.45f * fs_;
        v.lp_a = 1.f - std::exp(-6.28318530718f * fc / fs_);
    }
    static float NoteFrequency(float note) { return 440.f * std::exp2((note - 69.f) / 12.f); }
    void Update() {
        attack_alpha_ = 1.f - std::exp(-1.f / (fs_ * AttackTau(params_.solo_attack)));
        release_alpha_ = 1.f - std::exp(-1.f / (fs_ * ReleaseTau(params_.solo_release)));
        decay_alpha_ = 1.f - std::exp(-1.f / (fs_ * kDecayTau));
        const float perc_tau = 0.05f * std::exp2(6.f * params_.solo_sustain_level);
        perc_alpha_ = 1.f - std::exp(-1.f / (fs_ * perc_tau));
        tremolo_inc_ = params_.solo_tremolo_rate_hz / fs_;
        pitch_alpha_ = 1.f - std::exp(-1.f / (fs_ * 0.005f));
        const float t = params_.solo_tone;
        saw_mix_ = t < 0.5f ? 2.f * t : 1.f;
        pulse_width_ = 0.5f - 0.35f * (t > 0.5f ? 2.f * t - 1.f : 0.f);
        cutoff_base_ = 500.f + 15000.f * t * t;
        for(auto& v : voices_)
            if(v.active)
                SetFrequency(v);
    }
    void Envelope(SoloVoice& v) {
        switch(v.stage) {
            case SoloVoice::Attack:
                v.env += (1.f - v.env) * attack_alpha_;
                if(v.env >= kAttackDone)
                    v.stage = params_.solo_sustain_mode == SoloHold ? SoloVoice::Sustain : SoloVoice::Decay;
                break;
            case SoloVoice::Decay: {
                if(params_.solo_sustain_mode == SoloPercussive) {
                    v.env += (0.f - v.env) * perc_alpha_;
                    if(v.env < kSilent)
                        v = SoloVoice{};
                    break;
                }
                const float s = params_.solo_sustain_mode == SoloHold ? 1.f : params_.solo_sustain_level;
                v.env += (s - v.env) * decay_alpha_;
                if(std::fabs(v.env - s) < kSilent)
                    v.stage = SoloVoice::Sustain;
                break;
            }
            case SoloVoice::Sustain: {
                const float s = params_.solo_sustain_mode == SoloHold ? 1.f : params_.solo_sustain_level;
                if(params_.solo_sustain_mode == SoloPercussive || std::fabs(v.env - s) >= kSilent)
                    v.stage = SoloVoice::Decay; // sustain level or mode changed while held
                break;
            }
            case SoloVoice::Release:
                v.env += (0.f - v.env) * release_alpha_;
                if(v.env < kSilent)
                    v = SoloVoice{};
                break;
        }
    }
    static float Blep(float t, float dt) {
        if(t < dt) {
            t /= dt;
            return t + t - t * t - 1.f;
        }
        if(t > 1.f - dt) {
            t = (t - 1.f) / dt;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }
    float Oscillator(SoloVoice& v) {
        const float dt = v.inc;
        float ph = v.phase + dt;
        if(ph >= 1.f)
            ph -= 1.f;
        v.phase = ph;
        const float saw = 2.f * ph - 1.f - Blep(ph, dt);
        float ph2 = ph + (1.f - pulse_width_);
        if(ph2 >= 1.f)
            ph2 -= 1.f;
        const float pulse = (ph < pulse_width_ ? 1.f : -1.f) + Blep(ph, dt) - Blep(ph2, dt) +
                            (2.f * pulse_width_ - 1.f); // remove the pulse DC
        const float x = pulse + (saw - pulse) * saw_mix_;
        const float a = v.lp_a;
        v.lp1 += (x - v.lp1) * a;
        v.lp2 += (v.lp1 - v.lp2) * a;
        return v.lp2;
    }

    float fs_ = 48000.f;
    InstrumentParams params_;
    SoloVoice voices_[kSoloVoices];
    float tremolo_phase_ = 0.f, tremolo_inc_ = 0.f;
    float attack_alpha_ = 0.f, release_alpha_ = 0.f, decay_alpha_ = 0.f, perc_alpha_ = 0.f;
    float saw_mix_ = 1.f, pulse_width_ = 0.5f, cutoff_base_ = 4000.f;
    float pitch_target_ = 0.f, pitch_current_ = 0.f, pitch_factor_ = 1.f, pitch_alpha_ = 0.f;
    uint64_t clock_ = 0;
};

} // namespace streichwurst
