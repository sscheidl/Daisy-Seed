#pragma once
// SPDX-License-Identifier: MIT
// Multimode zero-delay-feedback ladder filter. Four trapezoidal one-poles with global
// feedback, solved in closed form every sample (no iteration). Modes are mixes of the taps.
//
// Clean core (drive 0, age 0): the only nonlinearity is the rational limiter on the stored
// state of the first stage, with a level derived from the loop so that self-oscillation has
// the same level at every cutoff and in every mode.
// Drive adds saturation inside the loop. Each nonlinearity becomes a gain taken from the
// previous sample, so the solve stays linear and closed-form: Model::Sum at the summing
// point (cheap), Model::Stages also in every stage. Harder input pushes back on resonance.
// Age adds component tolerance, drift and jitter from a seed.
// Derivation, sources and measurements: docs/DESIGN.md.
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
// Where drive acts. Clean ignores it; Sum is the cheap path; Stages is the full ladder.
enum class Model : uint8_t { Clean, Sum, Stages };
inline constexpr unsigned kModelCount = 3;

inline constexpr float kOscLevel = 0.5f;      // peak level of self-oscillation at resonance 100 %
inline constexpr float kLevelTrim = 1.06f;    // centres the measured level error (DESIGN.md 4)
inline constexpr float kNotchFeedback = 3.8f; // Notch: its zero sits on the pole; stay below 4
inline constexpr float kSlowMs = 3.f;         // settling of resonance, mode and bass compensation
inline constexpr float kNoiseLevel = 1e-6f;   // -120 dBFS: starts the oscillation, stops denormals
// Drive (DESIGN.md 5): input gain up to 2^kDriveOctaves with half of it taken back behind the
// filter, the knee of the saturator, and the feedback at resonance 100 %, which has to rise
// with drive because the saturators lose loop gain.
inline constexpr float kDriveOctaves = 5.f;     // +30 dB
inline constexpr float kDriveKnee = 0.5f;       // the saturator's input scale
inline constexpr float kDriveFullAt = 0.125f;   // the knee is fully in from here on
inline constexpr float kDriveFeedback = 8.f;    // feedback at resonance 100 % and full knee
inline constexpr float kDriveLimiter = 3.f;     // the limiter stands back by 1 + this at full knee
// Age (DESIGN.md 6). All at age 100 %: the first three scale with age^3, jitter with age^5.
inline constexpr float kAgeTolerance = 0.25f;   // +- share of g per stage, and of the feedback
inline constexpr float kAgeDrift = 8.f;         // +- semitones
inline constexpr float kAgeNoise = 1000.f;      // times kNoiseLevel: -60 dBFS
inline constexpr float kAgeJitter = 2.f;        // +- semitones per call
inline constexpr float kAgeDriftFrames = 4800.f; // a new drift target every 100 ms
inline constexpr float kAgeDriftMs = 300.f;      // glide towards it

struct Parameters {
    float cutoff_note = 69.f; // MIDI note units, fractions allowed; 69 = 440 Hz
    float resonance = 0.f;    // 0..1; self-oscillation starts at 4 / kMaxFeedback
    float bass_comp = 1.f;    // c in the input gain 1 + c k: 0 thin, 0.5 half, 1 keeps the bass
    Mode mode = Mode::Lp24;
    float drive = 0.f;        // 0..1: 0 is the clean core
    float age = 0.f;          // 0..1: 0 is the new filter, 1 is broken on purpose
    Model model = Model::Stages;
};

// x a^2 / (a^2 + x^2) = x / (1 + x^2 / a^2) up to its maximum a / 2 at x = a, held beyond,
// where the curve would fall again. The slope is 0 there: a smooth join, and one division.
inline float RationalLimit(float x, float inv_a2, float half_a) {
    const float d = 1.f + inv_a2 * x * x;
    return d < 2.f ? x / d : (x > 0.f ? half_a : -half_a);
}

class Ladder {
  public:
    Ladder() { Seed(0x2545F491u); }
    explicit Ladder(uint32_t seed) { Seed(seed); }

    // Sets the noise and this instance's component tolerances. Give every channel and voice
    // its own seed. The same seed gives the same filter.
    void Seed(uint32_t seed) {
        noise_ = seed;
        age_rng_ = seed ^ 0x9E3779B9u;
        for(float& t : tolerance_)
            t = Random();
    }
    void Reset() {
        for(float& s : state_)
            s = 0.f;
        for(float& p : previous_)
            p = 0.f;
        snap_ = true;
    }
    void Set(const Parameters& p) {
        note_target_ = p.cutoff_note == p.cutoff_note ? p.cutoff_note : 69.f;
        const Mode mode = unsigned(p.mode) < kModeCount ? p.mode : Mode::Lp24;
        const Model model = unsigned(p.model) < kModelCount ? p.model : Model::Stages;
        const float drive = model == Model::Clean ? 0.f : Clamp01(p.drive);
        slow_target_ = Slow(mode, Clamp01(p.resonance), Clamp01(p.bass_comp), drive);
        grit_target_ = Grit(mode, Clamp01(p.resonance), drive);
        model_ = model;
        age_target_ = Clamp01(p.age);
    }
    // The per-voice cutoff input: any source (envelope, matrix, key tracking) adds up in note
    // units here. Far cheaper than Set(), so it can run every call.
    void SetCutoff(float note) { note_target_ = note == note ? note : 69.f; }

    // Coefficients per sample: the cutoff ramps linearly from the previous call's target to
    // this one's, and resonance, mode and bass compensation move without a step. `fm`
    // (optional) adds that many semitones per sample: exponential audio-rate cutoff input.
    // In place (in == out) is allowed.
    void Process(const float* in, float* out, std::size_t frames, const float* fm = nullptr) {
        if(!frames)
            return;
        Advance(frames);
        if(Plain())
            RunCleanRamped(in, out, frames, fm);
        else if(drive_on_ && model_ == Model::Stages)
            RunRamped<Model::Stages>(in, out, frames, fm);
        else if(drive_on_)
            RunRamped<Model::Sum>(in, out, frames, fm);
        else
            RunRamped<Model::Clean>(in, out, frames, fm);
        Finish();
    }

    // Cheaper path for many voices: all coefficients are computed once per call and held.
    // The cutoff moves in steps of one call, so the host picks the call size (8 samples =
    // 6 kHz control rate).
    void ProcessHeld(const float* in, float* out, std::size_t frames) {
        if(!frames)
            return;
        Advance(frames);
        if(Plain())
            RunCleanHeld(in, out, frames);
        else if(drive_on_ && model_ == Model::Stages)
            RunHeld<Model::Stages>(in, out, frames);
        else if(drive_on_)
            RunHeld<Model::Sum>(in, out, frames);
        else
            RunHeld<Model::Clean>(in, out, frames);
        Finish();
    }

    const float* State() const { return state_; }
#ifdef FILTER_CORE_COST_PROBE // tools/cost_probe.cpp: single paths for the instruction count
    float CleanForCost(float x, const float* c) {
        Loop loop = Load();
        const float y = Tick(loop, x, c[0], c[1], c[2], c[3], c + 4);
        Store(loop);
        return y;
    }
    template <Model kModel> float GeneralForCost(float x, const float* c) {
        Loop loop = Load();
        const float y = General<kModel>(loop, x, *reinterpret_cast<const Stage*>(c + 4 + kSlowCount), c[0], c[1],
                                        c[2], c[3], c + 4);
        Store(loop);
        return y;
    }
    void AdvanceForCost(std::size_t n) { Advance(n); }
    void SetupForCost() { // what every held call computes before its loop
        Stage stage;
        const NoteRow row = LookUp(note_next_);
        Stages(stage, row.g, Spread());
        Reciprocals(stage);
        previous_[0] = stage.inv[0] + stage.inv[1] + stage.inv[2] + stage.inv[3];
    }
#endif
    float Tolerance(unsigned stage) const { return tolerance_[stage & 3]; } // -1..1, from the seed

  private:
    // Everything that mode and resonance set; these values move slowly and are interpolated.
    enum { kFeedback, kTune, kPre, kLevel, kInvLevel2, kMixX, kMixU, kMix1, kMix2, kMix3, kMix4, kSlowCount };
    struct SlowValues {
        float v[kSlowCount];
    };
    // Drive values; these move with the same settling but stay constant within a call.
    struct GritValues {
        float gain = 1.f, out = 1.f, knee2 = 0.f;
    };
    // The running state as locals: `out` could alias a member, which would force a reload
    // of the whole state after every sample.
    struct Loop {
        float s0, s1, s2, s3;
        uint32_t noise;
    };
    // Per-stage values of the general path.
    struct Stage {
        float g[4];   // with this instance's tolerance
        float inv[4]; // 1 / (1 + g), for models without a saturating stage
    };

    static float Clamp01(float x) { return x > 0.f ? (x < 1.f ? x : 1.f) : 0.f; }
    // Non-finite or absurd input counts as silence, so the state stays finite.
    static float Sanitize(float x) { return x > -1e4f && x < 1e4f ? x : 0.f; }
    static float Knee(float drive) {
        const float position = drive * (1.f / kDriveFullAt);
        return position < 1.f ? position : 1.f;
    }
    // 0 below the oscillation threshold, 1 at resonance 100 %.
    static float TopRange(float r) {
        constexpr float start = 4.f / kMaxFeedback;
        return r > start ? (r - start) * (1.f / (1.f - start)) : 0.f;
    }

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

    // The same under saturation, with the higher feedback of the top range; measured.
    static float DriveTapGain(Mode mode) {
        switch(mode) {
            case Mode::Hp: return 2.7f;
            case Mode::Phaser: return 7.8f;
            default: return TapGain(mode);
        }
    }

    static SlowValues Slow(Mode mode, float r, float c, float drive) {
        SlowValues s{};
        float* v = s.v;
        float k = (mode == Mode::Notch ? kNotchFeedback : kMaxFeedback) * r;
        const float linear_k = k;
        const float knee = Knee(drive);
        // The top of the range keeps its start at 100 / 1.1 %, and reaches further with drive.
        if(knee > 0.f && mode != Mode::Notch && k > 4.f)
            k = 4.f + TopRange(r) * (kMaxFeedback - 4.f + knee * (kDriveFeedback - kMaxFeedback));
        v[kFeedback] = k;
        // The limiter's leak pulls the oscillation sharp by a factor 1 + ln(k / 4) / 4
        // (DESIGN.md 3); -3 log2(k / 4) semitones takes that back. Saturation does not.
        v[kTune] = k > 4.f ? -3.f * Log2(k * 0.25f) * (1.f - knee) : 0.f;
        v[kPre] = 1.f;
        // The limiter leaks a little of the first stage, which fills the stopband of the
        // cancelling tap mixes. It is only needed near oscillation, so its level rises as
        // the feedback falls: calibrated at k = kMaxFeedback, out of the way at k = 0.
        float open = linear_k * (1.f / kMaxFeedback);
        open = open > 0.01f ? open : 0.01f;
        const float level = kOscLevel * kLevelTrim * (1.f + kDriveLimiter * knee * knee * knee) / (TapGain(mode) * open);
        v[kLevel] = level;
        v[kInvLevel2] = 1.f / (level * level);
        const float bass = 1.f + c * linear_k;
        switch(mode) {
            case Mode::Lp6: v[kMix1] = 1.f; v[kPre] = bass; break;
            case Mode::Lp12: v[kMix2] = 1.f; v[kPre] = bass; break;
            case Mode::Lp18: v[kMix3] = 1.f; v[kPre] = bass; break;
            case Mode::Lp24: v[kMix4] = 1.f; v[kPre] = bass; break;
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
                v[kMix1] = -4.f + c * linear_k;
                v[kMix2] = 12.f - 2.f * c * linear_k;
                v[kMix3] = -16.f + 2.f * c * linear_k;
                v[kMix4] = 8.f;
                break;
        }
        return s;
    }

    // Input gain, the part of it taken back at the output, and the saturator's knee. Below
    // the oscillation half the gain is taken back, so drive also raises quiet signals. At
    // resonance 100 % none is, which keeps self-oscillation at its level.
    static GritValues Grit(Mode mode, float r, float drive) {
        GritValues g;
        if(drive <= 0.f)
            return g;
        const float knee = Knee(drive) * kDriveKnee, top = mode == Mode::Notch ? 0.f : TopRange(r);
        const float octaves = kDriveOctaves * drive;
        g.gain = Exp2(octaves);
        // While the knee fades in, the limiter still sets the level, and it has the tap gain.
        g.out = Exp2(-0.5f * octaves * (1.f - top)) / (1.f + top * Knee(drive) * (DriveTapGain(mode) - 1.f));
        g.knee2 = knee * knee;
        return g;
    }

    float Random() {
        age_rng_ = age_rng_ * 1664525u + 1013904223u;
        return float(int32_t(age_rng_)) * (1.f / 2147483648.f);
    }
    bool Plain() const { return !drive_on_ && age_ <= 0.f; }

    // Moves the block's end values towards the targets; the first call jumps to them.
    void Advance(std::size_t frames) {
        if(snap_) {
            slow_ = slow_next_ = slow_target_;
            grit_ = grit_target_;
            note_ = note_next_ = note_target_;
            age_ = age_target_;
            snap_ = false;
        } else {
            float a = float(frames) * (1000.f / (kSlowMs * float(kSampleRate)));
            a = a < 1.f ? a : 1.f;
            for(unsigned i = 0; i < kSlowCount; ++i)
                slow_next_.v[i] = slow_.v[i] + a * (slow_target_.v[i] - slow_.v[i]);
            grit_.gain += a * (grit_target_.gain - grit_.gain);
            grit_.out += a * (grit_target_.out - grit_.out);
            grit_.knee2 += a * (grit_target_.knee2 - grit_.knee2);
            // These two end exactly at the target, so that 0 means the clean path again.
            if(grit_target_.knee2 <= 0.f && grit_.knee2 < 1e-6f)
                grit_ = grit_target_;
            const float step = float(frames) * (1000.f / (30.f * float(kSampleRate)));
            const float move = age_target_ - age_;
            age_ += move > step ? step : (move < -step ? -step : move);
            note_next_ = note_target_;
        }
        drive_on_ = grit_.knee2 > 0.f;
        age_note_ = 0.f;
        if(age_ > 0.f)
            AdvanceAge(frames);
        note_next_ = note_target_ + age_note_;
    }
    // Drift and jitter at block rate, and the per-stage values that follow from the age.
    void AdvanceAge(std::size_t frames) {
        const float a3 = age_ * age_ * age_, a5 = a3 * age_ * age_;
        drift_timer_ += float(frames);
        if(drift_timer_ >= kAgeDriftFrames) {
            drift_timer_ -= kAgeDriftFrames;
            drift_target_ = Random();
        }
        drift_ += float(frames) * (1000.f / (kAgeDriftMs * float(kSampleRate))) * (drift_target_ - drift_);
        age_note_ = kAgeDrift * a3 * drift_ + kAgeJitter * a5 * Random();
    }

    void Finish() {
        slow_ = slow_next_;
        note_ = note_next_;
        // Must be unreachable with sanitized input; never leave a non-finite value in the state.
        const float sum = state_[0] + state_[1] + state_[2] + state_[3];
        if(!(sum - sum == 0.f))
            Reset();
    }
    void Store(const Loop& loop) {
        state_[0] = loop.s0;
        state_[1] = loop.s1;
        state_[2] = loop.s2;
        state_[3] = loop.s3;
        noise_ = loop.noise;
    }
    Loop Load() const { return {state_[0], state_[1], state_[2], state_[3], noise_}; }

    // ----- clean core ---------------------------------------------------------------------
    void RunCleanRamped(const float* in, float* out, std::size_t frames, const float* fm) {
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
        Store(loop);
    }
    void RunCleanHeld(const float* in, float* out, std::size_t frames) {
        float v[kSlowCount];
        for(unsigned i = 0; i < kSlowCount; ++i)
            v[i] = slow_next_.v[i];
        const NoteRow row = LookUp(note_next_ + v[kTune]);
        const float g2 = row.G * row.G, sol = 1.f / (1.f + v[kFeedback] * g2 * g2);
        const float inv_a2 = row.inv_a2 * v[kInvLevel2], half_a = row.half_a * v[kLevel];
        Loop loop = Load();
        for(std::size_t n = 0; n < frames; ++n)
            out[n] = Tick(loop, Sanitize(in[n]), row.G, sol, inv_a2, half_a, v);
        Store(loop);
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

    // ----- general path: per-stage g, saturation as a gain from the previous sample ---------
    // tolerance * age^3 on g per stage, once per call.
    void Stages(Stage& s, float g, float spread) const {
        for(unsigned i = 0; i < 4; ++i) {
            s.g[i] = g * (1.f + spread * tolerance_[i]);
        }
    }
    static void Reciprocals(Stage& s) { // four reciprocals for one division
        const float a = 1.f + s.g[0], b = 1.f + s.g[1], c = 1.f + s.g[2], d = 1.f + s.g[3];
        const float ab = a * b, cd = c * d, r = 1.f / (ab * cd), rab = r * cd, rcd = r * ab;
        s.inv[0] = rab * b;
        s.inv[1] = rab * a;
        s.inv[2] = rcd * d;
        s.inv[3] = rcd * c;
    }
    float Spread() const { return kAgeTolerance * age_ * age_ * age_; }
    // The feedback's own tolerance, from the same seed.
    float FeedbackScale(float spread) const { return 1.f + spread * tolerance_[0] * tolerance_[3]; }

    template <Model kModel>
    void RunRamped(const float* in, float* out, std::size_t frames, const float* fm) {
        const float inv_frames = 1.f / float(frames);
        float v[kSlowCount], step[kSlowCount];
        for(unsigned i = 0; i < kSlowCount; ++i) {
            step[i] = (slow_next_.v[i] - slow_.v[i]) * inv_frames;
            v[i] = slow_.v[i];
        }
        float note = note_;
        const float note_step = (note_next_ - note_) * inv_frames;
        const float spread = Spread(), feedback = FeedbackScale(spread);
        const float noise = kNoiseLevel * (1.f + kAgeNoise * age_ * age_ * age_);
        Loop loop = Load();
        Stage stage;
        for(std::size_t n = 0; n < frames; ++n) {
            note += note_step;
            for(unsigned i = 0; i < kSlowCount; ++i)
                v[i] += step[i];
            const NoteRow row = LookUp(note + v[kTune] + (fm ? fm[n] : 0.f));
            Stages(stage, row.g, spread);
            if(kModel != Model::Stages)
                Reciprocals(stage);
            out[n] = General<kModel>(loop, Sanitize(in[n]), stage, v[kFeedback] * feedback,
                                     row.inv_a2 * v[kInvLevel2], row.half_a * v[kLevel], noise, v);
        }
        Store(loop);
    }
    template <Model kModel>
    void RunHeld(const float* in, float* out, std::size_t frames) {
        float v[kSlowCount];
        for(unsigned i = 0; i < kSlowCount; ++i)
            v[i] = slow_next_.v[i];
        const NoteRow row = LookUp(note_next_ + v[kTune]);
        const float spread = Spread(), k = v[kFeedback] * FeedbackScale(spread);
        const float noise = kNoiseLevel * (1.f + kAgeNoise * age_ * age_ * age_);
        const float inv_a2 = row.inv_a2 * v[kInvLevel2], half_a = row.half_a * v[kLevel];
        Stage stage;
        Stages(stage, row.g, spread);
        Reciprocals(stage);
        Loop loop = Load();
        for(std::size_t n = 0; n < frames; ++n)
            out[n] = General<kModel>(loop, Sanitize(in[n]), stage, k, inv_a2, half_a, noise, v);
        Store(loop);
    }

    // The saturator as a gain: SaturateSoft(x) / x = (27 + x^2) / (27 + 9 x^2) from
    // common/dsp/saturation.h. Without the clamp used there it is still monotonic (its slope
    // has a double zero at x = 3, then rises to 1 / 9), so the loop gain never reaches zero.
    static float Squared(float knee2, float x) {
        const float a = knee2 * x * x;
        return a < 1e4f ? a : 1e4f; // keeps the shared-division products in range
    }

    template <Model kModel>
    float General(Loop& l, float x, const Stage& st, float k, float inv_a2, float half_a, float noise, const float* v) {
        l.noise = l.noise * 1664525u + 1013904223u;
        const float seed = float(int32_t(l.noise)) * (noise / 2147483648.f);
        const float xin = grit_.gain * v[kPre] * x + seed;
        const float knee2 = grit_.knee2;
        float t0 = 1.f, t1 = 1.f, t2 = 1.f, t3 = 1.f, r1 = st.inv[0], r2 = st.inv[1], r3 = st.inv[2], r4 = st.inv[3];
        if(kModel == Model::Sum) {
            const float a = Squared(knee2, previous_[0]);
            t0 = (27.f + a) / (27.f + 9.f * a);
        } else if(kModel == Model::Stages) {
            const float a0 = Squared(knee2, previous_[0]), a1 = Squared(knee2, previous_[1]);
            const float a2 = Squared(knee2, previous_[2]), a3 = Squared(knee2, previous_[3]);
            const float a4 = Squared(knee2, previous_[4]);
            // Five reciprocals for one division.
            const float d0 = 27.f + 9.f * a0, d1 = 27.f + 9.f * a1, d2 = 27.f + 9.f * a2, d3 = 27.f + 9.f * a3,
                        d4 = 27.f + 9.f * a4;
            const float p01 = d0 * d1, p23 = d2 * d3, p0123 = p01 * p23, r = 1.f / (p0123 * d4);
            const float r0123 = r * d4, r01 = r0123 * p23, r23 = r0123 * p01;
            t0 = (27.f + a0) * (r01 * d1);
            t1 = (27.f + a1) * (r01 * d0);
            t2 = (27.f + a2) * (r23 * d3);
            t3 = (27.f + a3) * (r23 * d2);
            const float t4 = (27.f + a4) * (r * p0123);
            // y = (s + g t_in in) / (1 + g t_out): four more reciprocals for one division.
            const float e1 = 1.f + st.g[0] * t1, e2 = 1.f + st.g[1] * t2, e3 = 1.f + st.g[2] * t3,
                        e4 = 1.f + st.g[3] * t4;
            const float p12 = e1 * e2, p34 = e3 * e4, q = 1.f / (p12 * p34), q12 = q * p34, q34 = q * p12;
            r1 = q12 * e2;
            r2 = q12 * e1;
            r3 = q34 * e4;
            r4 = q34 * e3;
        }
        // y_i = A_i y_(i-1) + B_i, then the same closed solve as the clean core.
        const float A1 = st.g[0] * t0 * r1, A2 = st.g[1] * t1 * r2, A3 = st.g[2] * t2 * r3, A4 = st.g[3] * t3 * r4;
        const float B1 = l.s0 * r1, B2 = l.s1 * r2, B3 = l.s2 * r3, B4 = l.s3 * r4;
        const float P = A1 * A2 * A3 * A4, Q = A4 * (A3 * (A2 * B1 + B2) + B3) + B4;
        const float y4 = (P * xin + Q) / (1.f + k * P);
        const float u = xin - k * y4;
        const float y1 = A1 * u + B1, y2 = A2 * y1 + B2, y3 = A3 * y2 + B3;
        const float s1 = 2.f * y1 - l.s0;
        l.s1 = 2.f * y2 - l.s1;
        l.s2 = 2.f * y3 - l.s2;
        l.s3 = 2.f * y4 - l.s3;
        l.s0 = RationalLimit(s1, inv_a2, half_a);
        previous_[0] = u;
        previous_[1] = y1;
        previous_[2] = y2;
        previous_[3] = y3;
        previous_[4] = y4;
        return grit_.out *
               (v[kMixX] * xin + v[kMixU] * u + v[kMix1] * y1 + v[kMix2] * y2 + v[kMix3] * y3 + v[kMix4] * y4);
    }

    float state_[4] = {0.f, 0.f, 0.f, 0.f};
    float previous_[5] = {0.f, 0.f, 0.f, 0.f, 0.f}; // u, y1..y4 of the last sample
    float tolerance_[4] = {0.f, 0.f, 0.f, 0.f};     // -1..1, fixed per instance
    SlowValues slow_{}, slow_next_{}, slow_target_ = Slow(Mode::Lp24, 0.f, 1.f, 0.f);
    GritValues grit_, grit_target_;
    float note_ = 69.f, note_next_ = 69.f, note_target_ = 69.f;
    float age_ = 0.f, age_target_ = 0.f, age_note_ = 0.f;
    float drift_ = 0.f, drift_target_ = 0.f, drift_timer_ = 0.f;
    uint32_t noise_ = 0x2545F491u, age_rng_ = 0u;
    Model model_ = Model::Stages;
    bool snap_ = true, drive_on_ = false;
};

} // namespace seed::dsp::filter
