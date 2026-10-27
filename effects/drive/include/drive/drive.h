#pragma once
// SPDX-License-Identifier: MIT
// Drive: the MixForge curves of common/dsp/saturation.h with tone and auto level
// (ARBEITSAUFTRAG 4.7, R14). Wet-only under the FX contract 1.0: Prepare is startup-only,
// Process is bounded and allocation-free, the host owns dry/wet, the 10 ms on/off fade
// and output protection (drive/host.h). Auto level is measured around this core only:
// input of the drive versus the output of shaper and tone, before Level.
//   1. a fixed compensation per curve over the level that drives the curve, i.e. drive
//      gain plus the input RMS of the last 300 ms window (autolevel_table.h, measured on
//      the phase 0 reference fixtures). A table over the drive alone cannot hold +-1 dB
//      for inputs of different loudness (CHANGELOG 0.1.0),
//   2. a slow correction from 300 ms RMS windows, limited to +-3 dB, held below -60 dBFS,
//   3. the target is input RMS + target offset; Compensation scales the correction in dB.
// No libm in the audio path (common/dsp/fastmath.h): bit-identical on every host.
#include "../../../../common/audio/fx_contract.h"
#include "../../../../common/dsp/fastmath.h"
#include "../../../../common/dsp/saturation.h"
#include "autolevel_table.h"
#include <cstdint>

namespace seed::fx {
namespace drive {
inline constexpr unsigned kSampleRate = 48000;
inline constexpr float kMaxDriveDb = 30.f;     // drive 100 % = +30 dB into the curve
inline constexpr unsigned kWindowFrames = 14400; // 300 ms RMS window
inline constexpr float kSilenceDb = -60.f, kSlowLimitDb = 3.f;
inline constexpr float kParamSmoothMs = 20.f, kGainSmoothMs = 50.f;
inline constexpr float kDefaultInputDb = -18.f; // assumed input RMS before the first window

enum class Curve : uint8_t { Soft, Hard, Tape, Fold };

// Values in their units (contracts/parameters.json, component drive).
struct Parameters {
    bool enabled = true;             // the host fades the wet path in and out (10 ms)
    Curve curve = Curve::Soft;
    float drive = 25.f;              // %
    float tone = 70.f;               // %: 0 = 1 kHz low-pass, 100 = 20 kHz
    float level_db = 0.f;            // -24..12
    bool auto_level = true;
    float compensation = 100.f;      // %
    float target_offset_db = -1.5f;  // -3..0
};

// Soft and Tape are rational curves that reach +-1 at +-3 and rise again beyond; the
// drive keeps their input inside +-3, where they saturate (Tape FX limits its drive gain
// instead). Hard and Fold take any input.
inline float Shape(Curve curve, float x) {
    switch(curve) {
        case Curve::Hard: return seed::dsp::SaturateHard(x);
        case Curve::Tape: return seed::dsp::SaturateTape(seed::dsp::SaturateClamp(x, -3.f, 3.f));
        case Curve::Fold: return seed::dsp::SaturateFold(x);
        default: return seed::dsp::SaturateSoft(seed::dsp::SaturateClamp(x, -3.f, 3.f));
    }
}

// Level reduction of a curve in dB at the RMS level `level_db` into the curve (input RMS
// plus drive gain), linear between the table points kLevelMinDb, +kLevelStepDb, ...
inline float CurveReductionDb(Curve curve, float level_db) {
    float pos = (level_db - kLevelMinDb) / kLevelStepDb;
    const float last = float(kLevelPoints - 1);
    pos = pos < 0.f ? 0.f : (pos > last ? last : pos);
    const int i = pos >= last ? kLevelPoints - 2 : int(pos);
    const float frac = pos - float(i);
    const float* row = kAutoLevelDb[unsigned(curve) & 3];
    return row[i] + frac * (row[i + 1] - row[i]);
}
// Fixed compensation: the curve's reduction minus the drive gain in front of it.
inline float StaticDb(Curve curve, float drive, float input_db) {
    const float drive_db = drive * 0.01f * kMaxDriveDb;
    return CurveReductionDb(curve, input_db + drive_db) - drive_db;
}
} // namespace drive

class Drive {
  public:
    using Parameters = drive::Parameters;
    struct Storage {}; // no delay memory

    bool Prepare(audio::Config config, Storage&) {
        if(config.sample_rate != drive::kSampleRate || !config.max_block)
            return false;
        config_ = config;
        ready_ = true;
        param_k_ = seed::dsp::SmoothingCoefficient(drive::kParamSmoothMs, float(drive::kSampleRate));
        gain_k_ = seed::dsp::SmoothingCoefficient(drive::kGainSmoothMs, float(drive::kSampleRate));
        Reset();
        return true;
    }
    void SetParameters(const Parameters& p) { requested_ = Sanitize(p); }
    const Parameters& Requested() const { return requested_; }
    void Reset() {
        tone_lp_[0] = tone_lp_[1] = 0.f;
        dc_x_[0] = dc_x_[1] = dc_y_[0] = dc_y_[1] = 0.f;
        in_sq_ = core_sq_ = 0.f;
        frames_ = 0;
        slow_db_ = 0.f;
        input_db_ = drive::kDefaultInputDb;
        snap_ = true;
    }
    // Observation for tests and the panel.
    float StaticDb() const { return drive::StaticDb(requested_.curve, drive_, input_db_); }
    float InputDb() const { return input_db_; }
    float SlowDb() const { return slow_db_; }
    float AppliedDb() const { return applied_db_; }

    // Raw core without auto level and level (table generation, tests).
    void SetRawMode(bool raw) { raw_ = raw; }

    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block))
            return false;
        const Parameters& p = requested_;
        const float target_tone = ToneHz(p.tone);
        if(snap_) {
            drive_ = p.drive;
            tone_hz_ = target_tone;
            applied_db_ = TargetDb(p);
            snap_ = false;
        }
        const float level = raw_ ? 1.f : seed::dsp::DbToGain(p.level_db);
        for(std::size_t n = 0; n < block.frames; ++n) {
            drive_ += param_k_ * (p.drive - drive_);
            tone_hz_ += param_k_ * (target_tone - tone_hz_);
            if((n & 7) == 0) { // control rate 6 kHz for the exp2-based gains
                pre_gain_ = seed::dsp::DbToGain(drive_ * 0.01f * drive::kMaxDriveDb);
                // One-pole low-pass: a = 1 - exp(-2 pi fc / fs).
                tone_a_ = 1.f - seed::dsp::Exp2(-1.44269504f * 6.28318531f * tone_hz_ / float(drive::kSampleRate));
                out_gain_ = raw_ ? 1.f : seed::dsp::DbToGain(applied_db_) * level;
            }
            // Non-finite or absurd input counts as silence (bounded, finite output).
            const float in_l = block.left[n] > -1e4f && block.left[n] < 1e4f ? block.left[n] : 0.f;
            const float in_r = block.right[n] > -1e4f && block.right[n] < 1e4f ? block.right[n] : 0.f;
            tone_lp_[0] += tone_a_ * (drive::Shape(p.curve, in_l * pre_gain_) - tone_lp_[0]);
            tone_lp_[1] += tone_a_ * (drive::Shape(p.curve, in_r * pre_gain_) - tone_lp_[1]);
            // DC blocker (~5 Hz): the Tape curve's bias and asymmetric clipping leave an offset.
            const float core_l = tone_lp_[0] - dc_x_[0] + 0.99935f * dc_y_[0];
            const float core_r = tone_lp_[1] - dc_x_[1] + 0.99935f * dc_y_[1];
            dc_x_[0] = tone_lp_[0];
            dc_x_[1] = tone_lp_[1];
            dc_y_[0] = core_l;
            dc_y_[1] = core_r;
            Measure(in_l, in_r, core_l, core_r, p);
            applied_db_ += gain_k_ * (TargetDb(p) - applied_db_);
            block.out_left[n] = core_l * out_gain_;
            block.out_right[n] = core_r * out_gain_;
        }
        // A non-finite input must not poison the state (contract: bounded, finite output).
        if(!(tone_lp_[0] == tone_lp_[0]) || !(tone_lp_[1] == tone_lp_[1]) || !(dc_y_[0] == dc_y_[0]) ||
           !(dc_y_[1] == dc_y_[1]) || !(slow_db_ == slow_db_) || !(applied_db_ == applied_db_))
            Reset();
        return true;
    }

  private:
    static Parameters Sanitize(Parameters p) {
        const auto clamp = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
        p.curve = drive::Curve(unsigned(p.curve) & 3);
        p.drive = clamp(p.drive, 0.f, 100.f);
        p.tone = clamp(p.tone, 0.f, 100.f);
        p.level_db = clamp(p.level_db, -24.f, 12.f);
        p.compensation = clamp(p.compensation, 0.f, 100.f);
        p.target_offset_db = clamp(p.target_offset_db, -3.f, 0.f);
        return p;
    }
    static float ToneHz(float tone) { return 1000.f * seed::dsp::Exp2(tone * 0.01f * 4.32192809f); } // 1..20 kHz
    float TargetDb(const Parameters& p) const {
        if(!p.auto_level)
            return 0.f;
        return p.compensation * 0.01f * (drive::StaticDb(p.curve, drive_, input_db_) + slow_db_ + p.target_offset_db);
    }
    void Measure(float in_l, float in_r, float core_l, float core_r, const Parameters& p) {
        in_sq_ += in_l * in_l + in_r * in_r;
        core_sq_ += core_l * core_l + core_r * core_r;
        if(++frames_ < drive::kWindowFrames)
            return;
        const float scale = 1.f / float(2 * drive::kWindowFrames);
        const float in_db = 3.01029996f * seed::dsp::Log2(in_sq_ * scale + 1e-20f);
        const float core_db = 3.01029996f * seed::dsp::Log2(core_sq_ * scale + 1e-20f);
        in_sq_ = core_sq_ = 0.f;
        frames_ = 0;
        if(in_db < drive::kSilenceDb)
            return; // hold in silence
        input_db_ = in_db;
        float target = in_db - core_db - drive::StaticDb(p.curve, drive_, in_db);
        target = target < -drive::kSlowLimitDb ? -drive::kSlowLimitDb
                                               : (target > drive::kSlowLimitDb ? drive::kSlowLimitDb : target);
        slow_db_ += 0.5f * (target - slow_db_);
    }

    audio::Config config_;
    Parameters requested_;
    bool ready_ = false, snap_ = true, raw_ = false;
    float drive_ = 25.f, tone_hz_ = 10000.f, pre_gain_ = 1.f, tone_a_ = 1.f, out_gain_ = 1.f;
    float tone_lp_[2] = {0.f, 0.f}, dc_x_[2] = {0.f, 0.f}, dc_y_[2] = {0.f, 0.f};
    float param_k_ = 0.f, gain_k_ = 0.f;
    float in_sq_ = 0.f, core_sq_ = 0.f, slow_db_ = 0.f, applied_db_ = 0.f, input_db_ = drive::kDefaultInputDb;
    unsigned frames_ = 0;
};

} // namespace seed::fx
