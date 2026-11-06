#pragma once
// SPDX-License-Identifier: MIT
// Multimode zero-delay-feedback ladder filter. Four trapezoidal one-poles with global
// feedback, solved in closed form every sample (no iteration), and a single nonlinearity:
// the rational limiter x a^2 / (a^2 + x^2) on the stored state of the first stage. It acts
// on the next sample, so the solve stays linear. a is derived from the loop so that
// self-oscillation has the same level at every cutoff and in every mode. Modes are mixes
// of the taps. Derivation, sources and measurements: docs/DESIGN.md.
//
// No hardware access, no allocation, no libm: bit-identical on every host. Mono: stereo
// and polyphony are several instances. Header-only, kSampleRate only.
#include "../../../../common/dsp/fastmath.h"
#include "note_table.h"
#include "version.h"
#include <cstddef>
#include <cstdint>

namespace seed::dsp::filter {

enum class Mode : uint8_t { Lp6, Lp12, Lp18, Lp24, Bp, Hp, Notch, Phaser };
inline constexpr unsigned kModeCount = 8;

inline constexpr float kOscLevel = 0.5f;      // peak level of self-oscillation at resonance 100 %
inline constexpr float kLevelTrim = 1.06f;    // centres the measured level error (DESIGN.md 4)
inline constexpr float kNotchFeedback = 3.8f; // Notch: its zero sits on the pole; stay below 4
inline constexpr float kSlowMs = 3.f;         // settling of resonance, mode and bass compensation
inline constexpr float kNoiseLevel = 1e-6f;   // -120 dBFS: starts the oscillation, stops denormals

struct Parameters {
    float cutoff_note = 69.f; // MIDI note units, fractions allowed; 69 = 440 Hz
    float resonance = 0.f;    // 0..1; self-oscillation starts at 4 / kMaxFeedback
    float bass_comp = 1.f;    // c in the input gain 1 + c k: 0 thin, 0.5 half, 1 keeps the bass
    Mode mode = Mode::Lp24;
};

// x a^2 / (a^2 + x^2) = x / (1 + x^2 / a^2) up to its maximum a / 2 at x = a, held beyond,
// where the curve would fall again. The slope is 0 there: a smooth join, and one division.
inline float RationalLimit(float x, float inv_a2, float half_a) {
    const float d = 1.f + inv_a2 * x * x;
    return d < 2.f ? x / d : (x > 0.f ? half_a : -half_a);
}

class Ladder {
  public:
    Ladder() = default;
    // A different seed per instance decorrelates the -120 dBFS noise of the channels.
    explicit Ladder(uint32_t seed) : noise_(seed) {}

    void Reset() {
        for(float& s : state_)
            s = 0.f;
        snap_ = true;
    }
    void Set(const Parameters& p) {
        note_target_ = p.cutoff_note == p.cutoff_note ? p.cutoff_note : 69.f;
        const Mode mode = unsigned(p.mode) < kModeCount ? p.mode : Mode::Lp24;
        slow_target_ = Slow(mode, Clamp01(p.resonance), Clamp01(p.bass_comp));
    }

    // Coefficients per sample: the cutoff ramps linearly from the previous call's target to
    // this one's, and resonance, mode and bass compensation move without a step. `fm`
    // (optional) adds that many semitones per sample: exponential audio-rate cutoff input.
    // In place (in == out) is allowed.
    void Process(const float* in, float* out, std::size_t frames, const float* fm = nullptr) {
        if(!frames)
            return;
        Advance(frames);
        const float inv_frames = 1.f / float(frames);
        float v[kSlowCount], step[kSlowCount];
        for(unsigned i = 0; i < kSlowCount; ++i) {
            step[i] = (slow_next_.v[i] - slow_.v[i]) * inv_frames;
            v[i] = slow_.v[i];
        }
        float note = note_;
        const float note_step = (note_next_ - note_) * inv_frames;
        Loop loop = Load();
        for(std::size_t n = 0; n < frames; ++n) {
            note += note_step;
            for(unsigned i = 0; i < kSlowCount; ++i)
                v[i] += step[i];
            const NoteRow row = LookUp(note + v[kTune] + (fm ? fm[n] : 0.f));
            const float g2 = row.G * row.G, sol = 1.f / (1.f + v[kFeedback] * g2 * g2);
            out[n] = Tick(loop, Sanitize(in[n]), row.G, sol, row.inv_a2 * v[kInvLevel2], row.half_a * v[kLevel], v);
        }
        Finish(loop);
    }

    // Cheaper path for many voices: all coefficients are computed once per call and held,
    // which leaves one division per sample. Cutoff moves in steps of one call, so the host
    // picks the call size (8 samples = 6 kHz control rate).
    void ProcessHeld(const float* in, float* out, std::size_t frames) {
        if(!frames)
            return;
        Advance(frames);
        float v[kSlowCount];
        for(unsigned i = 0; i < kSlowCount; ++i)
            v[i] = slow_next_.v[i];
        const NoteRow row = LookUp(note_next_ + v[kTune]);
        const float g2 = row.G * row.G, sol = 1.f / (1.f + v[kFeedback] * g2 * g2);
        const float inv_a2 = row.inv_a2 * v[kInvLevel2], half_a = row.half_a * v[kLevel];
        Loop loop = Load();
        for(std::size_t n = 0; n < frames; ++n)
            out[n] = Tick(loop, Sanitize(in[n]), row.G, sol, inv_a2, half_a, v);
        Finish(loop);
    }

    const float* State() const { return state_; }

  private:
    // Everything that mode and resonance set; these values move slowly and are interpolated.
    enum { kFeedback, kTune, kPre, kLevel, kInvLevel2, kMixX, kMixU, kMix1, kMix2, kMix3, kMix4, kSlowCount };
    struct SlowValues {
        float v[kSlowCount];
    };
    // The running state as locals: `out` could alias a member, which would force a reload
    // of the whole state after every sample.
    struct Loop {
        float s0, s1, s2, s3;
        uint32_t noise;
    };
    Loop Load() const { return {state_[0], state_[1], state_[2], state_[3], noise_}; }

    static float Clamp01(float x) { return x > 0.f ? (x < 1.f ? x : 1.f) : 0.f; }
    // Non-finite or absurd input counts as silence, so the state stays finite.
    static float Sanitize(float x) { return x > -1e4f && x < 1e4f ? x : 0.f; }

    // Level of a mode's tap mix relative to the fourth stage while the loop oscillates. The
    // cutoff sits 41 cents below the oscillation (kTune), so each stage contributes
    // 1 / (1 + j u) with u = 1 + ln(kMaxFeedback / 4) / 4 = 1.024 (DESIGN.md 4). The limiter
    // scales with the inverse, so that every mode's output oscillates at kOscLevel.
    static float TapGain(Mode mode) {
        switch(mode) {
            case Mode::Lp6: return 2.93f;    // |1 + j u|^3
            case Mode::Lp12: return 2.05f;   // |1 + j u|^2
            case Mode::Lp18: return 1.43f;   // |1 + j u|
            case Mode::Bp: return 4.19f;     // 4 u^2
            case Mode::Hp: return 0.9f;      // measured: the first stage's leak weighs most here
            case Mode::Phaser: return 4.4f;  // |1 + j u|^4, plus half of the bass term's 10 % at c = 1
            case Mode::Notch: return 0.05f;  // never oscillates: the limiter only guards, far above
            default: return 1.f;
        }
    }

    static SlowValues Slow(Mode mode, float r, float c) {
        SlowValues s{};
        float* v = s.v;
        const float k = (mode == Mode::Notch ? kNotchFeedback : kMaxFeedback) * r;
        v[kFeedback] = k;
        // The limiter's leak pulls the oscillation sharp by a factor 1 + ln(k / 4) / 4
        // (DESIGN.md 3); -3 log2(k / 4) semitones takes that back.
        v[kTune] = k > 4.f ? -3.f * Log2(k * 0.25f) : 0.f;
        v[kPre] = 1.f;
        // The limiter leaks a little of the first stage, which fills the stopband of the
        // cancelling tap mixes. It is only needed near oscillation, so its level rises as
        // the feedback falls: calibrated at k = kMaxFeedback, out of the way at k = 0.
        float open = k * (1.f / kMaxFeedback);
        open = open > 0.01f ? open : 0.01f;
        const float level = kOscLevel * kLevelTrim / (TapGain(mode) * open);
        v[kLevel] = level;
        v[kInvLevel2] = 1.f / (level * level);
        switch(mode) {
            case Mode::Lp6: v[kMix1] = 1.f; v[kPre] = 1.f + c * k; break;
            case Mode::Lp12: v[kMix2] = 1.f; v[kPre] = 1.f + c * k; break;
            case Mode::Lp18: v[kMix3] = 1.f; v[kPre] = 1.f + c * k; break;
            case Mode::Lp24: v[kMix4] = 1.f; v[kPre] = 1.f + c * k; break;
            case Mode::Bp: // 4 w^2 (1 - w)^2: 12 dB/oct on both sides, 0 dB at the cutoff for k = 0
                v[kMix2] = 4.f;
                v[kMix3] = -8.f;
                v[kMix4] = 4.f;
                break;
            case Mode::Hp: // (1 - w)^4: 24 dB/oct
                v[kMixU] = 1.f;
                v[kMix1] = -4.f;
                v[kMix2] = 6.f;
                v[kMix3] = -4.f;
                v[kMix4] = 1.f;
                break;
            case Mode::Notch: { // input minus a band-pass scaled to cancel it exactly at the cutoff
                const float alpha = 2.f * (1.f - 0.25f * k);
                v[kMixX] = 1.f;
                v[kMix1] = -alpha;
                v[kMix2] = alpha;
                break;
            }
            case Mode::Phaser: // (1 + (2 w - 1)^4) / 2 plus a term that restores the bass
                v[kMixU] = 1.f;
                v[kMix1] = -4.f + c * k;
                v[kMix2] = 12.f - 2.f * c * k;
                v[kMix3] = -16.f + 2.f * c * k;
                v[kMix4] = 8.f;
                break;
        }
        return s;
    }

    // Moves the block's end values towards the targets; the first call jumps to them.
    void Advance(std::size_t frames) {
        if(snap_) {
            slow_ = slow_next_ = slow_target_;
            note_ = note_next_ = note_target_;
            snap_ = false;
            return;
        }
        float a = float(frames) * (1000.f / (kSlowMs * float(kSampleRate)));
        a = a < 1.f ? a : 1.f;
        for(unsigned i = 0; i < kSlowCount; ++i)
            slow_next_.v[i] = slow_.v[i] + a * (slow_target_.v[i] - slow_.v[i]);
        note_next_ = note_target_;
    }
    void Finish(const Loop& loop) {
        slow_ = slow_next_;
        note_ = note_next_;
        state_[0] = loop.s0;
        state_[1] = loop.s1;
        state_[2] = loop.s2;
        state_[3] = loop.s3;
        noise_ = loop.noise;
        // Must be unreachable with sanitized input; never leave a non-finite value in the state.
        const float sum = loop.s0 + loop.s1 + loop.s2 + loop.s3;
        if(!(sum - sum == 0.f))
            Reset();
    }

    static float Tick(Loop& l, float x, float G, float sol, float inv_a2, float half_a, const float* v) {
        l.noise = l.noise * 1664525u + 1013904223u;
        const float xin = v[kPre] * x + float(int32_t(l.noise)) * (kNoiseLevel / 2147483648.f);
        const float inv = 1.f - G; // 1 / (1 + g)
        // y4 = G^4 u + S, with S the part that comes from the stored states.
        const float S = (((G * l.s0 + l.s1) * G + l.s2) * G + l.s3) * inv;
        const float u = (xin - v[kFeedback] * S) * sol; // the zero-delay solve
        const float y1 = G * u + inv * l.s0;
        const float y2 = G * y1 + inv * l.s1;
        const float y3 = G * y2 + inv * l.s2;
        const float y4 = G * y3 + inv * l.s3;
        const float s1 = 2.f * y1 - l.s0;
        l.s1 = 2.f * y2 - l.s1;
        l.s2 = 2.f * y3 - l.s2;
        l.s3 = 2.f * y4 - l.s3;
        l.s0 = RationalLimit(s1, inv_a2, half_a);
        return v[kMixX] * xin + v[kMixU] * u + v[kMix1] * y1 + v[kMix2] * y2 + v[kMix3] * y3 + v[kMix4] * y4;
    }

    float state_[4] = {0.f, 0.f, 0.f, 0.f};
    SlowValues slow_{}, slow_next_{}, slow_target_ = Slow(Mode::Lp24, 0.f, 1.f);
    float note_ = 69.f, note_next_ = 69.f, note_target_ = 69.f;
    uint32_t noise_ = 0x2545F491u;
    bool snap_ = true;
};

} // namespace seed::dsp::filter
