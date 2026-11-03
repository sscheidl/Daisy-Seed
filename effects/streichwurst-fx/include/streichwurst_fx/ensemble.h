#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst ensemble: one delay line for the summed string section (SF-D01) with the
// documented mode topology (SF-D03, SF-D05, SF-D07, SF-F06), own implementation:
//  * three parabolic LFOs (SF-D04) at 0.81 / 7.77 / 0.32 Hz (SF-D05), advanced once per
//    16-sample control hop; delay positions ramp linearly across the hop (SF-D06 fast path);
//  * mode 0: three combined taps (slow + fast LFO, phases 0, +-0x55550000), the first
//    replaces the input; mode 1: input + three combined + two chorus taps (phases 0,
//    +135 deg); mode 2: input + three chorus taps (0, +135, -135 deg);
//  * delays in seconds from the 40 kHz sample counts (248 -/+ ...) / 40000, so the
//    ensemble keeps its time behaviour at any sample rate (own time scaling);
//  * reading: own float linear interpolation at the real read position (the original
//    quantizes the difference before multiplying, SF-D06; not reproduced);
//  * stereo: the two cached taps of each mode go into the two arms with factor -21/128
//    (SF-D07), applied by the caller after the phaser, which works on the common sum;
//  * the relative output gain words of SF-F06 are reported for the end of the chain;
//  * own transition rule (SF-F08 own decision): line and LFOs always run, also in bypass;
//    mode and bypass changes cross-fade two tap sets of the same line over 20 ms.
// No feedback (SF-D06: none in the reader).
#include "params.h"
#include <cmath>
#include <cstddef>
#include <cstring>

namespace streichwurst_fx {

constexpr float kStereoTapFactor = 21.f / 128.f;
constexpr float kOffsetCombined = 1431633920.f / 4294967296.f; // 0x55550000 of a 32-bit turn
constexpr float kOffsetChorus = 0.375f;                         // 0x60000000 (135 degrees)
constexpr float kLfoSlowHz = 0.809999765f, kLfoFastHz = 7.769999793f, kLfoChorusHz = 0.319999526f;

struct EnsembleSum {
    float sum = 0.f;     // common sum F before the phaser
    float tap_l = 0.f;   // tap for the left arm correction
    float tap_r = 0.f;   // tap for the right arm correction
    float gain = 1.f;    // relative output gain word (SF-F06), cross-faded
};

class Ensemble {
  public:
    static constexpr int kLine = 1024; // >= (376 / 40000) * fs + 2 for fs <= 108 kHz
    static constexpr int kHop = 16;
    static constexpr float kFadeSeconds = 0.020f;

    bool Prepare(float sample_rate) {
        if(!(sample_rate >= 8000.f && sample_rate <= 96000.f))
            return false;
        fs_ = sample_rate;
        scale_ = fs_ / 40000.f;
        fade_step_ = 1.f / (kFadeSeconds * fs_);
        inc_slow_ = kLfoSlowHz * float(kHop) / fs_;
        inc_fast_ = kLfoFastHz * float(kHop) / fs_;
        inc_chorus_ = kLfoChorusHz * float(kHop) / fs_;
        Reset();
        return true;
    }
    void Reset() {
        std::memset(line_, 0, sizeof(line_));
        write_ = 0;
        hop_pos_ = 0;
        slow_ = fast_ = chorus_ = 0.f;
        cur_ = prev_ = target_;
        fade_ = 1.f;
        Positions(next_);
        std::memcpy(now_, next_, sizeof(now_));
        std::memcpy(step_, zero_, sizeof(step_));
    }
    void ClearLine() { std::memset(line_, 0, sizeof(line_)); }

    // Configuration change; the cross-fade starts at the next sample.
    void Configure(bool enabled, int mode) {
        const Config c{enabled, mode < 0 ? 0 : (mode > 2 ? 2 : mode)};
        if(c == target_)
            return;
        target_ = c;
        if(fade_ < 1.f && fade_ < 0.5f)
            cur_ = prev_; // a change during a fade restarts from the dominant set
        prev_ = cur_;
        cur_ = c;
        fade_ = 0.f;
    }

    EnsembleSum Process(float x) {
        if(hop_pos_ == 0)
            Advance();
        line_[write_] = x;
        float taps[kTaps];
        for(int k = 0; k < kTaps; ++k) {
            taps[k] = 0.f;
            if(Needed(k))
                taps[k] = Read(now_[k]);
            now_[k] += step_[k];
        }
        write_ = (write_ + 1) & (kLine - 1);
        if(++hop_pos_ == kHop)
            hop_pos_ = 0;
        EnsembleSum a = Mix(cur_, x, taps);
        if(fade_ < 1.f) {
            const EnsembleSum b = Mix(prev_, x, taps);
            const float f = fade_;
            a.sum = b.sum + (a.sum - b.sum) * f;
            a.tap_l = b.tap_l + (a.tap_l - b.tap_l) * f;
            a.tap_r = b.tap_r + (a.tap_r - b.tap_r) * f;
            a.gain = b.gain + (a.gain - b.gain) * f;
            fade_ += fade_step_;
            if(fade_ >= 1.f)
                fade_ = 1.f;
        }
        return a;
    }

    // Current delays in seconds (tests): 0..2 combined, 3..5 chorus (0, +135, -135 deg).
    float DelaySeconds(int tap) const { return now_[tap] / fs_; }
    bool Fading() const { return fade_ < 1.f; }
    static float GainWord(bool enabled, int mode) {
        if(!enabled)
            return 1.f; // -32768
        return mode == 0 ? 0.8125f : (mode == 1 ? 0.625f : 0.75f); // -26624, -20480, -24576
    }

  private:
    static constexpr int kTaps = 6;
    struct Config {
        bool enabled = true;
        int mode = Mode1;
        bool operator==(const Config& o) const { return enabled == o.enabled && mode == o.mode; }
    };

    bool UsesTap(const Config& c, int k) const {
        if(!c.enabled)
            return false;
        switch(c.mode) {
            case 0: return k < 3;
            case 1: return k < 5;
            default: return k >= 3;
        }
    }
    bool Needed(int k) const { return UsesTap(cur_, k) || (fade_ < 1.f && UsesTap(prev_, k)); }

    void Positions(float out[kTaps]) const {
        const float offsets[3] = {0.f, kOffsetCombined, -kOffsetCombined};
        for(int k = 0; k < 3; ++k) {
            const float d40 = 248.f - 64.f * Parabola(Wrap(slow_ + offsets[k])) - 8.f * Parabola(Wrap(fast_ + offsets[k]));
            out[k] = d40 * scale_;
        }
        const float chorus[3] = {0.f, kOffsetChorus, -kOffsetChorus};
        for(int k = 0; k < 3; ++k)
            out[3 + k] = (248.f + 128.f * Parabola(Wrap(chorus_ + chorus[k]))) * scale_;
    }
    void Advance() {
        slow_ = Wrap(slow_ + inc_slow_);
        fast_ = Wrap(fast_ + inc_fast_);
        chorus_ = Wrap(chorus_ + inc_chorus_);
        std::memcpy(now_, next_, sizeof(now_));
        Positions(next_);
        for(int k = 0; k < kTaps; ++k)
            step_[k] = (next_[k] - now_[k]) / float(kHop);
    }
    float Read(float delay) const {
        const float pos = float(write_) - delay;
        const float fl = std::floor(pos);
        const float f = pos - fl;
        const int i0 = int(fl) & (kLine - 1), i1 = (i0 + 1) & (kLine - 1);
        return line_[i0] + (line_[i1] - line_[i0]) * f;
    }
    static EnsembleSum Mix(const Config& c, float x, const float t[kTaps]) {
        EnsembleSum s;
        s.gain = GainWord(c.enabled, c.mode);
        if(!c.enabled) {
            s.sum = x;
            return s;
        }
        switch(c.mode) {
            case 0:
                s.sum = t[0] + t[1] + t[2];
                s.tap_l = t[1];
                s.tap_r = t[2];
                break;
            case 1:
                s.sum = x + t[0] + t[1] + t[2] + t[3] + t[4];
                s.tap_l = t[3];
                s.tap_r = t[4];
                break;
            default:
                s.sum = x + t[3] + t[4] + t[5];
                s.tap_l = t[4];
                s.tap_r = t[5];
                break;
        }
        return s;
    }

    float fs_ = 48000.f, scale_ = 1.2f, fade_step_ = 0.f;
    float inc_slow_ = 0.f, inc_fast_ = 0.f, inc_chorus_ = 0.f;
    float slow_ = 0.f, fast_ = 0.f, chorus_ = 0.f;
    float line_[kLine]{};
    int write_ = 0, hop_pos_ = 0;
    float now_[kTaps]{}, next_[kTaps]{}, step_[kTaps]{};
    static constexpr float zero_[kTaps] = {};
    Config target_, cur_, prev_;
    float fade_ = 1.f;
};

} // namespace streichwurst_fx
