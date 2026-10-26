#pragma once
// SPDX-License-Identifier: MIT
// Stereo Delay (ARBEITSAUFTRAG 4.8, R02, D7, D12): Ping-Pong (default; (L+R)/2 into the
// left line, feedback crossing sides) and Stereo L/R (two lines, own feedback). The delay
// time is the distance between two successive echoes: side to side for Ping-Pong, per
// channel for Stereo. Free 10 ms .. 2 s or synced note values 1/32 .. 1/1 (dotted and
// triplet) from the host tempo, the full range: a dotted whole note at 30 BPM is 12 s.
// Wet-only under the FX contract 1.0; the 2 x 12 s lines (4.6 MB) come from the host
// (SDRAM). Time changes glide at a limited rate; jumps of 10 % or more crossfade between
// two read pointers over 20 ms. Feedback 0..95 % with a soft clip in the loop, damping
// low-pass 20 kHz .. 1 kHz in the loop. No libm in the audio path.
#include "../../../../common/audio/fx_contract.h"
#include "../../../../common/dsp/fastmath.h"
#include "../../../../common/dsp/saturation.h"
#include <cstddef>
#include <cstdint>

namespace seed::fx {
namespace delay {
inline constexpr unsigned kSampleRate = 48000;
inline constexpr std::size_t kMaxSeconds = 12;
inline constexpr std::size_t kMargin = 64; // interpolation margin per line
inline constexpr std::size_t kLineFrames = kMaxSeconds * kSampleRate + kMargin;
inline constexpr float kMaxDelaySamples = float(kMaxSeconds * kSampleRate); // read limit
inline constexpr float kMinDelayMs = 10.f, kMaxFreeMs = 2000.f;
inline constexpr float kJumpRatio = 0.1f;            // >= 10 %: crossfade
inline constexpr unsigned kCrossfadeFrames = 960;    // 20 ms
inline constexpr float kGlideRate = 0.05f;           // samples of delay change per sample
inline constexpr float kMaxFeedback = 0.95f;

enum class Mode : uint8_t { PingPong, Stereo };

// Note values in the parameter order of contracts/parameters.json (note_value).
inline constexpr float kNoteQuarters[18] = {
    0.125f, 0.125f * 2.f / 3.f, 0.1875f,  // 1/32, 1/32T, 1/32.
    0.25f,  0.25f * 2.f / 3.f,  0.375f,   // 1/16
    0.5f,   0.5f * 2.f / 3.f,   0.75f,    // 1/8
    1.f,    2.f / 3.f,          1.5f,     // 1/4
    2.f,    4.f / 3.f,          3.f,      // 1/2
    4.f,    8.f / 3.f,          6.f,      // 1/1
};

struct Parameters {
    bool enabled = true;          // the host fades the feed (delay/host.h)
    Mode mode = Mode::PingPong;
    float time_ms = 375.f;        // free time 10..2000
    bool sync = true;
    int note_value = 8;           // 1/8 dotted
    float feedback = 40.f;        // % 0..95
    float mix = 25.f;             // % dry/wet, applied by the host
    float damping_hz = 6000.f;    // 1000..20000
};
} // namespace delay

class StereoDelay {
  public:
    using Parameters = delay::Parameters;
    // Two lines of 12 s plus margin, as floats: 4 608 512 bytes. The host places it (SDRAM).
    struct Storage {
        float line[2][delay::kLineFrames];
    };

    bool Prepare(audio::Config config, Storage& storage) {
        if(config.sample_rate != delay::kSampleRate || !config.max_block)
            return false;
        config_ = config;
        line_[0] = storage.line[0];
        line_[1] = storage.line[1];
        ready_ = true;
        Reset();
        return true;
    }
    void SetParameters(const Parameters& p) { requested_ = Sanitize(p); }
    const Parameters& Requested() const { return requested_; }
    // Host tempo for synced note values (samples per quarter note).
    void SetTempo(float samples_per_quarter) {
        if(samples_per_quarter > 0.f && samples_per_quarter < 1e7f)
            quarter_ = samples_per_quarter;
    }
    float Tempo() const { return quarter_; }
    // Clears the lines: startup only (4.6 MB), never in the audio callback.
    void Reset() {
        for(int c = 0; c < 2; ++c)
            for(std::size_t i = 0; i < delay::kLineFrames; ++i)
                line_[c][i] = 0.f;
        write_ = 0;
        damp_[0] = damp_[1] = 0.f;
        fade_left_ = 0;
        snap_ = true;
    }
    // Observation: the delay in samples now, the target, and whether it was clamped.
    float DelaySamples() const { return delay_; }
    float TargetSamples() const { return target_; }
    bool Clamped() const { return clamped_; }
    bool Crossfading() const { return fade_left_ > 0; }

    // Target delay in samples for a parameter set at the current tempo, before clamping.
    float WantedSamples(const Parameters& p) const {
        if(p.sync)
            return delay::kNoteQuarters[unsigned(p.note_value) % 18] * quarter_;
        return p.time_ms * 0.001f * float(delay::kSampleRate);
    }

    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block))
            return false;
        const Parameters& p = requested_;
        float wanted = WantedSamples(p);
        // The interpolation reads at most line length minus margin: exactly 12 s.
        const float limit = float(delay::kLineFrames - delay::kMargin);
        clamped_ = wanted > limit;
        if(clamped_)
            wanted = limit;
        if(wanted < 1.f)
            wanted = 1.f;
        target_ = wanted;
        if(snap_) {
            delay_ = target_;
            snap_ = false;
        }
        // Jumps of 10 % or more: crossfade from the old to the new read position.
        if(fade_left_ == 0 && delay_ > 0.f && (target_ - delay_ > delay::kJumpRatio * delay_ ||
                                               delay_ - target_ > delay::kJumpRatio * delay_)) {
            fade_from_ = delay_;
            delay_ = target_;
            fade_left_ = delay::kCrossfadeFrames;
        }
        const float fb = p.feedback * 0.01f;
        const float damp_a = 1.f - seed::dsp::Exp2(-1.44269504f * 6.28318531f * p.damping_hz / float(delay::kSampleRate));
        for(std::size_t n = 0; n < block.frames; ++n) {
            // Small changes glide at a limited rate (tempo drift, slow knob turns).
            if(fade_left_ == 0) {
                if(delay_ < target_)
                    delay_ = delay_ + delay::kGlideRate > target_ ? target_ : delay_ + delay::kGlideRate;
                else if(delay_ > target_)
                    delay_ = delay_ - delay::kGlideRate < target_ ? target_ : delay_ - delay::kGlideRate;
            }
            float tap_l = Read(0, delay_), tap_r = Read(1, delay_);
            if(fade_left_ > 0) {
                const float w = float(fade_left_) / float(delay::kCrossfadeFrames);
                tap_l = tap_l * (1.f - w) + Read(0, fade_from_) * w;
                tap_r = tap_r * (1.f - w) + Read(1, fade_from_) * w;
                --fade_left_;
            }
            const float in_l = Finite(block.left[n]), in_r = Finite(block.right[n]);
            // Damping low-pass and soft clip inside the loop keep feedback bounded.
            damp_[0] += damp_a * (tap_l - damp_[0]);
            damp_[1] += damp_a * (tap_r - damp_[1]);
            const float back_l = Clip(damp_[0] * fb), back_r = Clip(damp_[1] * fb);
            float into_l, into_r;
            if(p.mode == delay::Mode::PingPong) {
                into_l = 0.5f * (in_l + in_r) + back_r; // the right echo returns to the left
                into_r = back_l;
            } else {
                into_l = in_l + back_l;
                into_r = in_r + back_r;
            }
            line_[0][write_] = into_l;
            line_[1][write_] = into_r;
            if(++write_ == delay::kLineFrames)
                write_ = 0;
            block.out_left[n] = tap_l;
            block.out_right[n] = tap_r;
        }
        if(!(damp_[0] == damp_[0]) || !(damp_[1] == damp_[1]))
            damp_[0] = damp_[1] = 0.f;
        return true;
    }

  private:
    static Parameters Sanitize(Parameters p) {
        const auto clamp = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
        p.mode = p.mode == delay::Mode::Stereo ? delay::Mode::Stereo : delay::Mode::PingPong;
        p.time_ms = clamp(p.time_ms, delay::kMinDelayMs, delay::kMaxFreeMs);
        p.note_value = p.note_value < 0 ? 0 : (p.note_value > 17 ? 17 : p.note_value);
        p.feedback = clamp(p.feedback, 0.f, delay::kMaxFeedback * 100.f);
        p.mix = clamp(p.mix, 0.f, 100.f);
        p.damping_hz = clamp(p.damping_hz, 1000.f, 20000.f);
        return p;
    }
    static float Finite(float x) { return x > -1e4f && x < 1e4f ? x : 0.f; }
    // Soft clip of the returning echo: transparent at normal levels, bounded at any level.
    static float Clip(float x) { return 1.5f * seed::dsp::SaturateSoft(seed::dsp::SaturateClamp(x * (2.f / 3.f), -3.f, 3.f)); }
    // Linear interpolation `d` samples behind the write position (d >= 1).
    float Read(int channel, float d) const {
        float pos = float(write_) - d;
        if(pos < 0.f)
            pos += float(delay::kLineFrames);
        std::size_t i = std::size_t(pos);
        const float frac = pos - float(i);
        if(i >= delay::kLineFrames)
            i -= delay::kLineFrames;
        const std::size_t j = i + 1 == delay::kLineFrames ? 0 : i + 1;
        const float* line = line_[channel];
        return line[i] + frac * (line[j] - line[i]);
    }

    audio::Config config_;
    Parameters requested_;
    float* line_[2] = {nullptr, nullptr};
    std::size_t write_ = 0;
    bool ready_ = false, snap_ = true, clamped_ = false;
    float quarter_ = 24000.f; // 120 BPM
    float delay_ = 18000.f, target_ = 18000.f, fade_from_ = 0.f;
    unsigned fade_left_ = 0;
    float damp_[2] = {0.f, 0.f};
};

} // namespace seed::fx
