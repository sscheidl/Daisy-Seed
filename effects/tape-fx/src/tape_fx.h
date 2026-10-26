#pragma once
// SPDX-License-Identifier: MIT
// Stereo tape effect ported from the drumlogue master effects TapeVibe (core) and
// MixForge (optional stages), sscheidl/drumlogue_fx_units @ 0db9c37; the verbatim
// originals are in tests/reference. Board-, SDK- and UI-free and wet-only under
// common/audio/fx_contract.h: the host owns dry/wet, bypass and output protection
// (tape_mixer.h). Every constant is the TapeVibe/MixForge value at 48 kHz.
#include "../../../common/audio/fx_contract.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace seed::fx {
namespace tape {
inline constexpr unsigned kSampleRate = 48000;
inline constexpr std::size_t kDelaySize = 2048; // TapeVibe: 2 x 2048 floats
inline constexpr std::size_t kDelayMask = kDelaySize - 1;
inline constexpr float kPi = 3.14159265358979323846f;
static_assert((kDelaySize & kDelayMask) == 0, "the delay index wraps with a mask");

// TapeVibe HISS TYPE / DROP TYPE; the value is the drumlogue parameter value.
enum class Character : uint8_t { Studio, Tape, Cassette };
// Curve of the drive stage. TapeVibe is the original. The others are MixForge's SAT
// MODE curves; MixForge SOFT is the TapeVibe curve and is therefore not repeated.
enum class DriveCurve : uint8_t { TapeVibe, MixTape, Hard, Fold };

struct Parameters {
    // TapeVibe, normalized 0..1 (drumlogue percent * 0.01). Defaults: TapeVibe init values.
    float drive = .35f, tone = .5f, age = .4f, wow = .2f, flutter = .15f, hiss = .2f, dropout = .1f;
    Character hiss_type = Character::Tape, drop_type = Character::Tape;
    // Optional MixForge stages. The defaults keep the pure TapeVibe sound.
    DriveCurve curve = DriveCurve::TapeVibe;
    bool compressor = false; // stereo-linked, in front of the tape stage (MixForge order)
    float threshold_db = -18.f, ratio = 4.f, attack_ms = 10.f, release_ms = 300.f;
    bool bitcrush = false; // after the tape stage, with MixForge's post-crush lowpass
    int bits = 16;         // 4..16
    int crush_stride = 1;  // sample-and-hold period in samples, 1..8
    // The exact drumlogue conversion of an integer percent parameter.
    static float FromPercent(int percent) { return static_cast<float>(percent) * 0.01f; }
};

// Delay memory, provided by the host so it can choose the RAM region (16 KiB).
struct Storage {
    float frames[kDelaySize][2]; // L/R interleaved: one cache line serves both channels
};
static_assert(sizeof(Storage) == 16384, "TapeVibe's 2 x 2048 float delay lines");

inline float ClampFloat(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
inline float SaturateSoft(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float SaturateHard(float x) { return ClampFloat(x, -1.0f, 1.0f); }
inline float SaturateTape(float x) {
    const float biased = x + 0.055f;
    return SaturateSoft(biased) - 0.052f;
}
inline float SaturateFold(float x) {
    if(x > 1.0f || x < -1.0f)
        x = 2.0f * std::fabs(0.5f * x + 0.5f - std::floor(0.5f * x + 1.0f)) - 1.0f;
    return x;
}
// sin(x) for x in [0, 2 pi): quadrant folding and an odd Taylor polynomial to x^11
// (truncation error < 6e-8 on [-pi/2, pi/2]), about 12 flops instead of a libm call.
inline float Sine(float x) {
    if(x > kPi)
        x -= 2.0f * kPi;
    if(x > 0.5f * kPi)
        x = kPi - x;
    else if(x < -0.5f * kPi)
        x = -kPi - x;
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f +
                x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
}
inline float DbToLin(float db) { return std::pow(10.0f, db * 0.05f); }

// MixForge tape pre-emphasis (+) and de-emphasis (-) around its TAPE curve.
struct Emphasis {
    float lp = 0.0f;
    float Pre(float x) {
        lp += 0.18f * (x - lp);
        return x + 0.35f * (x - lp);
    }
    float De(float x) {
        lp += 0.10f * (x - lp);
        return x - 0.30f * (x - lp);
    }
};

// MixForge compressor: RMS-linked detector, 6 dB soft knee, smoothed gain.
class Compressor {
  public:
    struct Settings {
        float threshold_db = 0.0f, ratio = 1.0f, attack = 0.0f, release = 0.0f;
        float below_knee = 0.0f; // envelope certainly 3 dB or more under the threshold
    };
    static Settings Make(float threshold_db, float ratio, float attack_ms, float release_ms) {
        Settings s;
        // MixForge evaluates 20 log10(dbToLin(threshold)) per sample; it is block-constant.
        s.threshold_db = 20.0f * std::log10(DbToLin(threshold_db));
        s.ratio = ratio;
        s.attack = std::exp(-1.0f / (static_cast<float>(kSampleRate) * (attack_ms * 0.001f)));
        s.release = std::exp(-1.0f / (static_cast<float>(kSampleRate) * (release_ms * 0.001f)));
        // 1e-4 below the knee start (0.0009 dB) is far beyond log10f/powf rounding, so the
        // shortcut returns 1 only where MixForge's dB comparison returns 1 as well.
        s.below_knee = DbToLin(s.threshold_db - 3.0f) * 0.9999f;
        return s;
    }
    void Reset() {
        env_ = 0.0f;
        gain_ = 1.0f;
    }
    void Process(float& l, float& r, const Settings& s) {
        const float detector = std::sqrt(0.5f * (l * l + r * r) + 1.0e-12f);
        const float coef = detector > env_ ? s.attack : s.release;
        env_ = coef * env_ + (1.0f - coef) * detector;
        gain_ = 0.985f * gain_ + 0.015f * StaticGain(env_, s);
        l *= gain_;
        r *= gain_;
    }
    bool Finite() const { return std::isfinite(env_) && std::isfinite(gain_); }
    float Gain() const { return gain_; }
  private:
    static float StaticGain(float env, const Settings& s) {
        // Below the knee MixForge's gain is exactly 1; skip log10f()/powf() there.
        if(env + 1.0e-12f < s.below_knee)
            return 1.0f;
        const float env_db = 20.0f * std::log10(env + 1.0e-12f);
        const float over_db = env_db - s.threshold_db;
        const float knee_db = 6.0f;
        if(2.0f * over_db <= -knee_db)
            return 1.0f;
        if(2.0f * over_db >= knee_db)
            return DbToLin((1.0f / s.ratio - 1.0f) * over_db);
        const float knee_pos = over_db + 0.5f * knee_db;
        return DbToLin((1.0f / s.ratio - 1.0f) * knee_pos * knee_pos / (2.0f * knee_db));
    }
    float env_ = 0.0f, gain_ = 1.0f;
};

// MixForge bitcrusher: sample-and-hold, rounding quantizer, one-pole lowpass.
class Bitcrusher {
  public:
    void Reset() {
        hold_l_ = hold_r_ = lp_l_ = lp_r_ = 0.0f;
        counter_ = 0;
    }
    void Process(float& l, float& r, int bits, int stride) {
        if(counter_ <= 0) {
            hold_l_ = Quantize(l, bits);
            hold_r_ = Quantize(r, bits);
            counter_ = stride;
        }
        --counter_;
        lp_l_ += 0.2f * (hold_l_ - lp_l_);
        lp_r_ += 0.2f * (hold_r_ - lp_r_);
        l = lp_l_;
        r = lp_r_;
    }
    static float Quantize(float x, int bits) {
        const int clamped = bits < 4 ? 4 : (bits > 16 ? 16 : bits);
        const float levels = static_cast<float>(1U << clamped);
        const float scaled = x * levels;
        return (scaled >= 0.0f ? std::floor(scaled + 0.5f) : std::ceil(scaled - 0.5f)) / levels;
    }
  private:
    float hold_l_ = 0.0f, hold_r_ = 0.0f, lp_l_ = 0.0f, lp_r_ = 0.0f;
    int counter_ = 0;
};
} // namespace tape

class TapeFx {
  public:
    using Parameters = tape::Parameters;
    using Storage = tape::Storage;

    bool Prepare(audio::Config config, Storage& storage) {
        ready_ = false;
        if(config.sample_rate != tape::kSampleRate || !config.max_block)
            return false; // TapeVibe's rates and filter coefficients are fixed at 48 kHz
        config_ = config;
        delay_ = storage.frames;
        ready_ = true;
        Reset();
        return true;
    }
    // Cheap request copy, applied at the next block. The continuous TapeVibe controls
    // glide (kGlideSeconds); switches and MixForge settings change at a block start.
    // The first block after Prepare()/Reset() starts at the requested values.
    void SetParameters(const Parameters& p) { requested_ = Sanitize(p); }
    const Parameters& Requested() const { return requested_; }
    // Clears all audio state and the noise generator, as TapeVibe's Reset().
    void Reset() {
        for(unsigned ch = 0; ch < 2; ++ch) {
            head_[ch] = tone_lp_[ch] = hiss_lp_[ch] = dropout_lp_[ch] = sat_env_[ch] = 0.0f;
            dropout_alpha_[ch] = 0.0f;
            emphasis_pre_[ch] = emphasis_de_[ch] = {};
        }
        if(delay_)
            for(std::size_t i = 0; i < tape::kDelaySize; ++i)
                delay_[i][0] = delay_[i][1] = 0.0f;
        index_ = 0;
        wow_phase_ = flutter_phase_a_ = flutter_phase_b_ = 0.0f;
        dropout_count_ = 0;
        dropout_gain_l_ = dropout_gain_r_ = 1.0f;
        rng_ = 0x12345678U;
        compressor_.Reset();
        crusher_.Reset();
        compressor_key_[0] = compressor_key_[1] = compressor_key_[2] = compressor_key_[3] = -1.0f;
        snap_ = true; // the first block after a reset starts at the requested values
    }
    // true: libm sinf() for the modulators, exactly as the drumlogue code. false
    // (default): polynomial sine on the same phases, see docs/REPORT.md.
    void SetReferenceMode(bool reference) { reference_ = reference; }
    // Read-only observation for tests and diagnostics.
    float LastDelaySamples() const { return last_delay_; }
    bool DropoutActive() const { return dropout_count_ > 0; }

    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block))
            return false;
        if(!block.frames)
            return true;
        const Parameters& p = requested_;
        // Block-rate glide of the continuous controls; linear inside the block. Once a
        // control has arrived it is exactly the requested value again.
        const Controls target = Controls::From(p);
        if(snap_) {
            current_ = target;
            snap_ = false;
        }
        const float k = 1.0f - std::exp(-static_cast<float>(block.frames) /
                                        (kGlideSeconds * static_cast<float>(tape::kSampleRate)));
        Controls start = current_, step{};
        bool ramp = false;
        for(unsigned i = 0; i < Controls::kCount; ++i) {
            float next = current_.v[i] + k * (target.v[i] - current_.v[i]);
            if(std::fabs(target.v[i] - next) <= 1.0e-5f)
                next = target.v[i];
            step.v[i] = (next - current_.v[i]) / static_cast<float>(block.frames);
            ramp |= step.v[i] != 0.0f;
            current_.v[i] = next;
        }
        if(p.compressor && (compressor_key_[0] != p.threshold_db || compressor_key_[1] != p.ratio ||
                            compressor_key_[2] != p.attack_ms || compressor_key_[3] != p.release_ms)) {
            compressor_settings_ = tape::Compressor::Make(p.threshold_db, p.ratio, p.attack_ms, p.release_ms);
            compressor_key_[0] = p.threshold_db; compressor_key_[1] = p.ratio;
            compressor_key_[2] = p.attack_ms; compressor_key_[3] = p.release_ms;
        }
        if(reference_)
            Run<true>(block, start, step, ramp);
        else
            Run<false>(block, start, step, ramp);
        if(!StateFinite())
            ClearState(); // Never keep NaN/Inf in feedback state; the host mutes this block.
        return true;
    }

  private:
    static constexpr float kGlideSeconds = 0.015f;
    // The continuous TapeVibe controls that glide.
    struct Controls {
        static constexpr unsigned kCount = 7;
        float v[kCount] = {};
        static Controls From(const Parameters& p) {
            return {{p.drive, p.tone, p.age, p.wow, p.flutter, p.hiss, p.dropout}};
        }
    };
    enum { kDrive, kTone, kAge, kWow, kFlutter, kHiss, kDropout };
    // Per-sample terms of TapeVibe, evaluated with its exact expressions.
    struct Derived {
        float drive_gain, post_gain, head_alpha, head_amount, compression_scale;
        float tone_alpha, tone_gain, base_delay, wow_depth, flutter_depth;
        float wow_increment, flutter_increment_a, flutter_increment_b;
        float hiss, hiss_age, dropout_probability;
        bool hiss_on, dropout_on;
    };
    Derived Derive(const Controls& c, tape::Character drop_type) const {
        const float drive = c.v[kDrive], tone = c.v[kTone], age = c.v[kAge];
        Derived d;
        d.drive_gain = 1.0f + drive * 9.0f;
        d.post_gain = (1.0f + 0.08f * drive) / (1.0f + 0.75f * drive + 0.2f * age);
        d.head_alpha = 0.010f + 0.010f * (1.0f - age);
        d.head_amount = 0.04f + 0.12f * age;
        d.compression_scale = 0.35f + 0.9f * age + 0.75f * drive;
        d.tone_alpha = 0.08f + (0.42f * tone) * (1.0f - 0.35f * age);
        // x - high * g == x + high * (-g) exactly, so one signed gain covers both shelves.
        d.tone_gain = tone < 0.5f ? -(0.85f * ((0.5f - tone) * 2.0f) + 0.15f * age)
                                  : 0.28f * ((tone - 0.5f) * 2.0f);
        d.base_delay = 10.0f + age * 8.0f;
        d.wow_depth = c.v[kWow] * (0.6f + 4.5f * age);
        d.flutter_depth = c.v[kFlutter] * (0.25f + 1.4f * age);
        const float rate = static_cast<float>(tape::kSampleRate);
        d.wow_increment = 2.0f * tape::kPi * (0.25f + 0.55f * age) / rate;
        d.flutter_increment_a = 2.0f * tape::kPi * (6.0f + 4.0f * age) / rate;
        d.flutter_increment_b = 2.0f * tape::kPi * (9.0f + 5.0f * age) / rate;
        d.hiss = c.v[kHiss];
        d.hiss_on = d.hiss > 0.0001f;
        d.hiss_age = 0.45f + 0.95f * age;
        const float dropout = c.v[kDropout];
        d.dropout_on = dropout > 0.001f;
        float probability = dropout * dropout * 0.00065f;
        if(drop_type == tape::Character::Tape)
            probability *= 1.8f;
        else if(drop_type == tape::Character::Cassette)
            probability *= 3.4f;
        probability *= 0.6f + 0.9f * age;
        d.dropout_probability = probability;
        return d;
    }

    template <bool kReference>
    void Run(const audio::StereoBlock& b, Controls c, const Controls& step, bool ramp) {
        const Parameters& p = requested_;
        Derived d = Derive(c, p.drop_type);
        const bool crush = p.bitcrush, compress = p.compressor;
        const tape::DriveCurve curve = p.curve;
        for(std::size_t i = 0; i < b.frames; ++i) {
            if(ramp) {
                for(unsigned j = 0; j < Controls::kCount; ++j)
                    c.v[j] += step.v[j];
                d = Derive(c, p.drop_type);
            }
            float l = b.left[i], r = b.right[i];
            if(compress)
                compressor_.Process(l, r, compressor_settings_);
            l = HeadBump(l, 0, d);
            r = HeadBump(r, 1, d);
            l = Drive(l, 0, d, curve);
            r = Drive(r, 1, d, curve);
            WowFlutter<kReference>(l, r, d);
            if(d.dropout_on)
                Dropout(l, r, d, p.drop_type);
            l = Tone(l, 0, d);
            r = Tone(r, 1, d);
            if(d.hiss_on) {
                l += Hiss(0, d, p.hiss_type);
                r += Hiss(1, d, p.hiss_type);
            }
            l *= d.post_gain;
            r *= d.post_gain;
            if(crush)
                crusher_.Process(l, r, p.bits, p.crush_stride);
            b.out_left[i] = l;
            b.out_right[i] = r;
        }
    }

    float HeadBump(float x, unsigned ch, const Derived& d) {
        head_[ch] += d.head_alpha * (x - head_[ch]);
        return x + head_[ch] * d.head_amount;
    }

    float Drive(float x, unsigned ch, const Derived& d, tape::DriveCurve curve) {
        sat_env_[ch] = 0.995f * sat_env_[ch] + 0.005f * std::fabs(x);
        const float compression = 1.0f / (1.0f + sat_env_[ch] * d.compression_scale);
        switch(curve) {
        case tape::DriveCurve::MixTape:
            x = emphasis_pre_[ch].Pre(x);
            return emphasis_de_[ch].De(tape::SaturateTape(x * d.drive_gain * compression));
        case tape::DriveCurve::Hard: return tape::SaturateHard(x * d.drive_gain * compression);
        case tape::DriveCurve::Fold: return tape::SaturateFold(x * d.drive_gain * compression);
        case tape::DriveCurve::TapeVibe:
        default: return tape::SaturateSoft(x * d.drive_gain * compression);
        }
    }

    static float WrapPhase(float phase) {
        while(phase >= 2.0f * tape::kPi)
            phase -= 2.0f * tape::kPi;
        while(phase < 0.0f)
            phase += 2.0f * tape::kPi;
        return phase;
    }

    template <bool kReference>
    void WowFlutter(float& l, float& r, const Derived& d) {
        // TapeVibe's float phase accumulators are kept exactly: their rounding sets the
        // true modulation rates. Only sinf() is replaced (tape::Sine, error < 1e-6).
        wow_phase_ = WrapPhase(wow_phase_ + d.wow_increment);
        flutter_phase_a_ = WrapPhase(flutter_phase_a_ + d.flutter_increment_a);
        flutter_phase_b_ = WrapPhase(flutter_phase_b_ + d.flutter_increment_b);
        const float wow = kReference ? std::sin(wow_phase_) : tape::Sine(wow_phase_);
        const float flutter_a = kReference ? std::sin(flutter_phase_a_) : tape::Sine(flutter_phase_a_);
        const float flutter_b = kReference ? std::sin(flutter_phase_b_) : tape::Sine(flutter_phase_b_);
        delay_[index_][0] = l;
        delay_[index_][1] = r;
        const float mod_delay = d.base_delay + d.wow_depth * wow +
                                d.flutter_depth * (0.7f * flutter_a + 0.3f * flutter_b);
        last_delay_ = mod_delay;
        // mod_delay stays within 3..26 samples, so the truncation is a floor.
        const int32_t base = static_cast<int32_t>(mod_delay);
        const float frac = mod_delay - static_cast<float>(base);
        const std::size_t read0 = (index_ - static_cast<std::size_t>(base)) & tape::kDelayMask;
        const std::size_t read1 = (read0 - 1) & tape::kDelayMask;
        l = delay_[read0][0] * (1.0f - frac) + delay_[read1][0] * frac;
        r = delay_[read0][1] * (1.0f - frac) + delay_[read1][1] * frac;
        index_ = (index_ + 1) & tape::kDelayMask;
    }

    float NextRand01() {
        rng_ = 1664525U * rng_ + 1013904223U;
        return static_cast<float>((rng_ >> 8) & 0x00FFFFFFU) / 16777215.0f;
    }

    void StartDropout(tape::Character type) {
        const float random = NextRand01();
        int32_t min_len = 40, max_len = 220;
        float min_gain = 0.45f, tone = 0.32f;
        if(type == tape::Character::Studio) {
            min_len = 24; max_len = 80; min_gain = 0.88f; tone = 0.10f;
        } else if(type == tape::Character::Cassette) {
            min_len = 64; max_len = 420; min_gain = 0.18f; tone = 0.55f;
        }
        dropout_count_ = min_len + static_cast<int32_t>((max_len - min_len) * random);
        dropout_gain_l_ = min_gain + (0.98f - min_gain) * NextRand01();
        dropout_gain_r_ = min_gain + (0.98f - min_gain) * NextRand01();
        if(type == tape::Character::Cassette && NextRand01() > 0.55f)
            dropout_gain_l_ *= 0.6f;
        if(type == tape::Character::Cassette && NextRand01() > 0.55f)
            dropout_gain_r_ *= 0.6f;
        dropout_alpha_[0] = dropout_alpha_[1] = 0.05f + tone * 0.20f;
    }

    void Dropout(float& l, float& r, const Derived& d, tape::Character type) {
        if(dropout_count_ <= 0 && NextRand01() < d.dropout_probability)
            StartDropout(type);
        if(dropout_count_ > 0) {
            dropout_lp_[0] += dropout_alpha_[0] * (l - dropout_lp_[0]);
            dropout_lp_[1] += dropout_alpha_[1] * (r - dropout_lp_[1]);
            l = dropout_lp_[0] * dropout_gain_l_;
            r = dropout_lp_[1] * dropout_gain_r_;
            --dropout_count_;
        }
    }

    float Tone(float x, unsigned ch, const Derived& d) {
        tone_lp_[ch] += d.tone_alpha * (x - tone_lp_[ch]);
        return x + (x - tone_lp_[ch]) * d.tone_gain;
    }

    float Hiss(unsigned ch, const Derived& d, tape::Character type) {
        const float noise = NextRand01() * 2.0f - 1.0f;
        float lp_alpha = 0.04f, level = 0.0045f;
        if(type == tape::Character::Studio) {
            lp_alpha = 0.015f; level = 0.0015f;
        } else if(type == tape::Character::Cassette) {
            lp_alpha = 0.08f; level = 0.0085f;
        }
        hiss_lp_[ch] += lp_alpha * (noise - hiss_lp_[ch]);
        return (noise - hiss_lp_[ch]) * level * d.hiss * d.hiss_age;
    }

    bool StateFinite() const {
        float sum = 0.0f;
        for(unsigned ch = 0; ch < 2; ++ch)
            sum += head_[ch] + tone_lp_[ch] + sat_env_[ch] + dropout_lp_[ch] + hiss_lp_[ch] +
                   emphasis_pre_[ch].lp + emphasis_de_[ch].lp;
        const std::size_t last = (index_ - 1) & tape::kDelayMask;
        sum += delay_[last][0] + delay_[last][1];
        return std::isfinite(sum) && compressor_.Finite();
    }
    void ClearState() {
        Reset();
        snap_ = false; // keep gliding from the current control values
    }

    static float Unit(float x, float fallback) {
        return std::isfinite(x) ? tape::ClampFloat(x, 0.0f, 1.0f) : fallback;
    }
    static float Range(float x, float lo, float hi, float fallback) {
        return std::isfinite(x) ? tape::ClampFloat(x, lo, hi) : fallback;
    }
    static Parameters Sanitize(Parameters p) {
        const Parameters d{};
        p.drive = Unit(p.drive, d.drive);
        p.tone = Unit(p.tone, d.tone);
        p.age = Unit(p.age, d.age);
        p.wow = Unit(p.wow, d.wow);
        p.flutter = Unit(p.flutter, d.flutter);
        p.hiss = Unit(p.hiss, d.hiss);
        p.dropout = Unit(p.dropout, d.dropout);
        if(static_cast<uint8_t>(p.hiss_type) > 2) p.hiss_type = d.hiss_type;
        if(static_cast<uint8_t>(p.drop_type) > 2) p.drop_type = d.drop_type;
        if(static_cast<uint8_t>(p.curve) > 3) p.curve = d.curve;
        // MixForge parameter ranges.
        p.threshold_db = Range(p.threshold_db, -60.f, 0.f, d.threshold_db);
        p.ratio = Range(p.ratio, 1.f, 20.f, d.ratio);
        p.attack_ms = Range(p.attack_ms, 1.f, 200.f, d.attack_ms);
        p.release_ms = Range(p.release_ms, 10.f, 2000.f, d.release_ms);
        p.bits = p.bits < 4 ? 4 : (p.bits > 16 ? 16 : p.bits);
        p.crush_stride = p.crush_stride < 1 ? 1 : (p.crush_stride > 8 ? 8 : p.crush_stride);
        return p;
    }

    audio::Config config_{};
    Parameters requested_{};
    Controls current_{};
    float (*delay_)[2] = nullptr;
    std::size_t index_ = 0;
    float head_[2] = {}, tone_lp_[2] = {}, hiss_lp_[2] = {}, dropout_lp_[2] = {}, sat_env_[2] = {};
    float dropout_alpha_[2] = {};
    tape::Emphasis emphasis_pre_[2] = {}, emphasis_de_[2] = {};
    float wow_phase_ = 0.0f, flutter_phase_a_ = 0.0f, flutter_phase_b_ = 0.0f;
    float last_delay_ = 0.0f;
    int32_t dropout_count_ = 0;
    float dropout_gain_l_ = 1.0f, dropout_gain_r_ = 1.0f;
    uint32_t rng_ = 0x12345678U;
    tape::Compressor compressor_;
    tape::Compressor::Settings compressor_settings_{};
    float compressor_key_[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    tape::Bitcrusher crusher_;
    bool ready_ = false, reference_ = false, snap_ = true;
};
} // namespace seed::fx
