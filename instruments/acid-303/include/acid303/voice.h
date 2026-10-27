#pragma once
// SPDX-License-Identifier: MIT
// Acid 303: monophonic TB-303-style voice (ARBEITSAUFTRAG 4.6). DaisySP PolyBLEP saw or
// square into DaisySP's LadderFilter (LP24, tanh input stage, 4x oversampled
// internally): an approximation, not a model of the 303's diode ladder (D4). Filter
// envelope with env mod and decay, accent (more envelope, more level, shorter decay),
// constant-time slide, and a shared note stack with a source per note (R16).
//
// Deterministic across hosts: no libm calls in the audio path (own exp2), so the golden
// hash of the host test does not depend on the C library. Header-only, no allocation.
#include "Filters/ladder.h"
#include "Synthesis/oscillator.h"
#include <cstdint>
#include <cstring>

namespace acid303 {

constexpr float kSampleRate = 48000.f;

// 2^x from integer and fractional parts: exact scaling by the exponent bits, the 6th-order
// series of 2^f on [-0.5, 0.5] (max. relative error ~2e-7). Only + * and bit operations.
inline float Exp2(float x) {
    if(x < -60.f)
        x = -60.f;
    if(x > 60.f)
        x = 60.f;
    // Nearest integer: f in [-0.5, 0.5] keeps the series error near 1e-7.
    int i = int(x + 0.5f);
    if(float(i) > x + 0.5f)
        --i;
    const float f = x - float(i);
    const float p = 1.f + f * (0.693147182f + f * (0.240226507f + f * (0.0555041087f +
                    f * (0.00961812911f + f * (0.00133335581f + f * 0.000154034f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, sizeof(bits));
    bits += uint32_t(i) << 23;
    float r;
    std::memcpy(&r, &bits, sizeof(r));
    return r;
}
// Per-sample coefficient of a one-pole that falls to 1/e^k over `ms` (k = ln-steps).
inline float DecayCoefficient(float ms, float log2_steps) {
    const float samples = ms * 0.001f * kSampleRate;
    return Exp2(-log2_steps / (samples < 1.f ? 1.f : samples));
}
inline float NoteFrequency(float note) { return 440.f * Exp2((note - 69.f) / 12.f); }

enum class Source : uint8_t { Sequencer, Preview, MidiIn };
enum class SlideMode : uint8_t { Off, Legato, Always };

// Parameter values in their units (contracts/parameters.json, component acid-303).
struct Params {
    int waveform = 0;          // 0 saw, 1 square
    int tune = 0;              // semitones -12..12
    float fine = 0.f;          // cents -100..100
    float cutoff = 800.f;      // Hz 40..12000
    float resonance = 50.f;    // % (100 % self-oscillates)
    float env_mod = 50.f;      // %
    float decay = 300.f;       // ms 30..3000
    float accent = 50.f;       // %
    float volume = 80.f;       // %
    float slide_ms = 60.f;     // ms 10..500
    SlideMode slide_mode = SlideMode::Legato; // external MIDI notes
};

class Voice {
  public:
    static constexpr int kStack = 8;            // held notes per source
    static constexpr float kAttackMs = 3.f, kReleaseMs = 8.f, kAmpHoldDecayMs = 4000.f;
    static constexpr float kAccentDecayMs = 200.f; // accent shortens the filter decay
    static constexpr float kSmoothMs = 20.f;      // parameter smoothing time constant
    static constexpr int kControlInterval = 8;  // filter coefficients every 8 samples (6 kHz)

    void Prepare() {
        osc_.Init(kSampleRate);
        osc_.SetWaveform(daisysp::Oscillator::WAVE_POLYBLEP_SAW);
        osc_.SetAmp(1.f);
        osc_.SetFreq(NoteFrequency(48));
        SetParams(params_); // Init() above reset the waveform
        filter_.Init(kSampleRate);
        filter_.SetFilterMode(daisysp::LadderFilter::FilterMode::LP24);
        // One-pole parameter smoothing at the control rate.
        smooth_ = 1.f - Exp2(-float(kControlInterval) / (kSmoothMs * 0.001f * kSampleRate) * 1.442695f);
        attack_step_ = 1.f / (kAttackMs * 0.001f * kSampleRate);
        release_ = DecayCoefficient(kReleaseMs, 1.442695f * 4.f);
        hold_ = DecayCoefficient(kAmpHoldDecayMs, 1.442695f * 4.f);
        Snap();
    }
    void SetParams(const Params& p) {
        params_ = p;
        osc_.SetWaveform(p.waveform ? daisysp::Oscillator::WAVE_POLYBLEP_SQUARE : daisysp::Oscillator::WAVE_POLYBLEP_SAW);
    }
    const Params& GetParams() const { return params_; }
    // Jump all smoothed values to their targets (start-up, tests).
    void Snap() {
        cutoff_oct_ = Log2Cutoff();
        res_ = params_.resonance;
        env_mod_ = params_.env_mod;
        volume_ = params_.volume;
        fine_ = params_.fine;
    }

    // ----- notes (TRANSPORT.md § 4) -----
    // legato: the sequencer's slide; MIDI notes glide by slide_mode.
    void NoteOn(uint8_t note, uint8_t velocity, bool accent, bool legato, Source source, uint32_t tag) {
        Remove(source, tag);
        Entry e{note, velocity, accent, source, tag, ++sequence_};
        Stack& s = stack_[Index(source)];
        if(s.count == kStack) { // oldest of this source falls out
            for(int i = 1; i < kStack; ++i)
                s.items[i - 1] = s.items[i];
            --s.count;
        }
        s.items[s.count++] = e;
        bool glide = legato;
        if(source == Source::MidiIn)
            glide = params_.slide_mode == SlideMode::Always ||
                    (params_.slide_mode == SlideMode::Legato && gate_);
        Sound(e, glide && gate_);
    }
    void NoteOff(uint32_t tag, Source source) {
        const bool was = gate_ && current_.tag == tag && current_.source == source;
        Remove(source, tag);
        if(was)
            Resume();
    }
    // MIDI input identifies notes by number: ends the newest held entry with that note.
    void NoteOffNote(uint8_t note, Source source) {
        const Stack& s = stack_[Index(source)];
        for(int i = s.count - 1; i >= 0; --i)
            if(s.items[i].note == note) {
                NoteOff(s.items[i].tag, source);
                return;
            }
    }
    // Panic of one source: only its notes end (R16).
    void AllNotesOff(Source source) {
        stack_[Index(source)].count = 0;
        if(gate_ && current_.source == source)
            Resume();
    }
    // Bootloader entry and USB loss: silence everything.
    void Mute() {
        for(auto& s : stack_)
            s.count = 0;
        gate_ = false;
    }

    // ----- audio -----
    float Process() { return Render(osc_.Process()); }
    void ProcessBlock(float* out, std::size_t n) {
        for(std::size_t i = 0; i < n; ++i)
            out[i] = Process();
    }
    // The voice path after the oscillator, also used by tests with another excitation.
    float Render(float source) {
        if(--control_ <= 0) {
            control_ = kControlInterval;
            Control();
        }
        // Amplitude envelope: 3 ms attack, slow fall while held, 8 ms release.
        if(gate_) {
            amp_ = amp_ + attack_step_ >= peak_ ? peak_ : amp_ + attack_step_;
            if(attacked_ || amp_ >= peak_) {
                attacked_ = true;
                peak_ *= hold_;
                amp_ = peak_;
            }
        } else {
            amp_ *= release_;
        }
        filter_env_ *= filter_decay_;
        float y = filter_.Process(source) * amp_ * gain_;
        // DC blocker (one pole at ~4 Hz): the envelope cuts cycles and leaves an offset.
        const float blocked = y - dc_x_ + 0.9995f * dc_y_;
        dc_x_ = y;
        dc_y_ = blocked;
        y = blocked;
        // Guard only: the PASS keeps the peak below 0 dBFS (test); never NaN/Inf out.
        if(!(y > -1.f))
            y = y != y ? 0.f : -1.f;
        if(y > 1.f)
            y = 1.f;
        return y;
    }

    bool Gate() const { return gate_; }
    float Pitch() const { return pitch_; }
    float FilterEnvelope() const { return filter_env_; }
    float CutoffHz() const { return cutoff_hz_; }
    uint8_t SoundingNote() const { return current_.note; }
    Source SoundingSource() const { return current_.source; }
    int Held(Source source) const { return stack_[Index(source)].count; }

  private:
    struct Entry {
        uint8_t note = 48, velocity = 100;
        bool accent = false;
        Source source = Source::Sequencer;
        uint32_t tag = 0, sequence = 0;
    };
    struct Stack {
        Entry items[kStack];
        int count = 0;
    };
    static int Index(Source s) { return s == Source::MidiIn ? 1 : 0; } // preview shares the sequencer's slot

    float Log2Cutoff() const {
        float hz = params_.cutoff < 40.f ? 40.f : (params_.cutoff > 12000.f ? 12000.f : params_.cutoff);
        // log2 by exponent and a short series; only used for smoothing in octaves.
        uint32_t bits;
        std::memcpy(&bits, &hz, sizeof(bits));
        const int e = int((bits >> 23) & 0xFF) - 127;
        bits = (bits & 0x007FFFFFu) | 0x3F800000u;
        float m;
        std::memcpy(&m, &bits, sizeof(m));
        const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
        const float ln = 2.f * t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f + t2 / 9.f))));
        return float(e) + ln * 1.442695041f;
    }
    void Remove(Source source, uint32_t tag) {
        Stack& s = stack_[Index(source)];
        int j = 0;
        for(int i = 0; i < s.count; ++i)
            if(s.items[i].tag != tag)
                s.items[j++] = s.items[i];
        s.count = j;
    }
    // The newest note still held in either source takes over (legato) or the gate closes.
    void Resume() {
        const Entry* best = nullptr;
        for(const Stack& s : stack_)
            for(int i = 0; i < s.count; ++i)
                if(!best || s.items[i].sequence > best->sequence)
                    best = &s.items[i];
        if(best)
            Sound(*best, true);
        else
            gate_ = false;
    }
    void Sound(const Entry& e, bool glide) {
        current_ = e;
        target_ = float(e.note);
        if(glide) {
            const float samples = params_.slide_ms * 0.001f * kSampleRate;
            glide_step_ = (target_ - pitch_) / (samples < 1.f ? 1.f : samples) * float(kControlInterval);
            glide_left_ = int(samples / float(kControlInterval)) + 1;
        } else {
            pitch_ = target_;
            glide_left_ = 0;
            Trigger(e.accent, e.velocity);
        }
        gate_ = true;
        Control();
    }
    void Trigger(bool accent, uint8_t velocity) {
        (void)velocity; // the 303 knows only normal and accented notes
        accent_now_ = accent ? params_.accent * 0.01f : 0.f;
        filter_env_ = 1.f;
        const float decay = accent && params_.decay > kAccentDecayMs ? kAccentDecayMs : params_.decay;
        filter_decay_ = DecayCoefficient(decay, 1.442695f * 2.302585f); // to 10 % in `decay`
        // Accent raises the level by up to +6 dB.
        peak_ = 1.f + accent_now_;
        amp_ = gate_ ? amp_ : 0.f;
        attacked_ = false;
    }
    void Control() {
        const float k = smooth_;
        cutoff_oct_ += k * (Log2Cutoff() - cutoff_oct_);
        res_ += k * (params_.resonance - res_);
        env_mod_ += k * (params_.env_mod - env_mod_);
        volume_ += k * (params_.volume - volume_);
        fine_ += k * (params_.fine - fine_);
        if(glide_left_ > 0) {
            pitch_ += glide_step_;
            if(--glide_left_ == 0)
                pitch_ = target_;
        }
        osc_.SetFreq(NoteFrequency(pitch_ + float(params_.tune) + fine_ * 0.01f));
        // Env mod: up to 4 octaves; accent adds up to 2 more (4.6).
        const float octaves = filter_env_ * (env_mod_ * 0.04f + accent_now_ * 2.f);
        float hz = Exp2(cutoff_oct_ + octaves);
        if(hz > 16000.f)
            hz = 16000.f;
        cutoff_hz_ = hz;
        filter_.SetFreq(hz);
        filter_.SetRes(res_ * 0.011f); // 100 % = 1.1: K = 4.4, above the self-oscillation point
        const float v = volume_ * 0.01f;
        gain_ = v * v * 0.5f;
    }

    daisysp::Oscillator osc_;
    daisysp::LadderFilter filter_;
    Params params_;
    Stack stack_[2];
    Entry current_;
    uint32_t sequence_ = 0;
    bool gate_ = false, attacked_ = false;
    float pitch_ = 48.f, target_ = 48.f, glide_step_ = 0.f;
    int glide_left_ = 0, control_ = 0;
    float amp_ = 0.f, peak_ = 1.f, attack_step_ = 0.f, release_ = 0.f, hold_ = 0.f;
    float filter_env_ = 0.f, filter_decay_ = 0.f, accent_now_ = 0.f;
    float cutoff_oct_ = 9.6f, res_ = 50.f, env_mod_ = 50.f, volume_ = 80.f, fine_ = 0.f;
    float cutoff_hz_ = 800.f, gain_ = 0.32f, smooth_ = 0.f, dc_x_ = 0.f, dc_y_ = 0.f;
};

} // namespace acid303
