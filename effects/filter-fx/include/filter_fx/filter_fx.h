#pragma once
// SPDX-License-Identifier: MIT
// Filter FX: Drive -> stereo ladder filter, with an LFO and an envelope follower on the
// cutoff. Wet-only under the FX contract 1.0: Prepare is startup-only, Process is bounded
// and allocation-free, the host owns dry/wet, the on/off fade and output protection
// (filter_fx/host.h). Everything sonic comes from shared components: the filter core
// (modules/filter-core), the Drive effect (effects/drive) and the LFO shapes of Mod on Daisy.
#include "../../../../common/audio/fx_contract.h"
#include "../../../../common/dsp/fastmath.h"
#include "../../../../modules/filter-core/include/filter_core/ladder.h"
#include "../../../../modules/mod-on-daisy/include/mod_on_daisy/sources.h"
#include "../../../drive/include/drive/drive.h"
#include "../../../drive/include/drive/host.h"
#include <cstdint>

namespace seed::fx {
namespace filter_fx {
using Mode = seed::dsp::filter::Mode;
inline constexpr std::size_t kMaxBlock = 48;
inline constexpr float kCutoffSmoothMs = 10.f; // pots and CC steps; the core ramps inside a block
inline constexpr float kEnvAttackMs = 5.f;
inline constexpr float kEnvFloorDb = -48.f;    // the follower spans this range up to 0 dBFS
inline constexpr float kDriveOffPercent = 0.05f; // below this the Drive stage is faded out
// Wet ceiling: unchanged up to kWetKnee, then the core's rational curve up to exactly 1.
inline constexpr float kWetKnee = 0.75f;

enum class LfoShape : uint8_t { Sine, Triangle, Saw, SampleHold };
enum class BassComp : uint8_t { Off, Half, Full };

// Values in their units (contracts/parameters.json, component filter-fx).
struct Parameters {
    bool enabled = true;      // the host fades the wet path in and out (10 ms)
    Mode mode = Mode::Lp24;
    float cutoff = 100.f;     // note, 24..135
    float resonance = 30.f;   // %
    float drive = 0.f;        // %
    float mix = 100.f;        // %, applied by the host
    float lfo_rate = 1.f;     // Hz, 0.05..20
    float lfo_depth = 0.f;    // semitones up and down, 0..48
    LfoShape lfo_shape = LfoShape::Sine;
    bool lfo_stereo = false;  // right LFO a quarter cycle later
    float env_amount = 0.f;   // semitones at full level, -48..48
    float env_release = 200.f; // ms, 10..2000
    BassComp bass_comp = BassComp::Full;
};

inline float WetCeiling(float x) {
    constexpr float span = 1.f - kWetKnee, a = 2.f * span, inv_a2 = 1.f / (a * a);
    if(x > kWetKnee)
        return kWetKnee + seed::dsp::filter::RationalLimit(x - kWetKnee, inv_a2, span);
    if(x < -kWetKnee)
        return -kWetKnee + seed::dsp::filter::RationalLimit(x + kWetKnee, inv_a2, span);
    return x;
}
} // namespace filter_fx

class FilterFx {
  public:
    using Parameters = filter_fx::Parameters;
    struct Storage { // the Drive stage's output between the two stages
        float left[filter_fx::kMaxBlock];
        float right[filter_fx::kMaxBlock];
    };

    bool Prepare(audio::Config config, Storage& storage) {
        if(config.sample_rate != seed::dsp::filter::kSampleRate || !config.max_block ||
           config.max_block > filter_fx::kMaxBlock)
            return false;
        Drive::Storage none;
        if(!drive_.Prepare(config, none))
            return false;
        config_ = config;
        storage_ = &storage;
        attack_k_ = seed::dsp::SmoothingCoefficient(filter_fx::kEnvAttackMs, float(seed::dsp::filter::kSampleRate));
        Reset();
        return true;
    }
    void SetParameters(const Parameters& p) { requested_ = Sanitize(p); }
    const Parameters& Requested() const { return requested_; }
    void Reset() {
        filter_[0].Reset();
        filter_[1].Reset();
        drive_.Reset();
        drive_mix_.Reset(requested_.drive > filter_fx::kDriveOffPercent);
        phase_[0] = 0.f;
        phase_[1] = 0.25f;
        hold_[0] = hold_[1] = 0.f;
        envelope_ = 0.f;
        snap_ = true;
    }
    // Observation for tests and the panel: the cutoff of each channel at the end of a block.
    float Note(unsigned channel) const { return note_[channel & 1]; }
    float Envelope() const { return envelope_; }

    bool Process(audio::StereoBlock block) {
        if(!storage_ || !audio::Valid(config_, block))
            return false;
        if(!block.frames)
            return true;
        const Parameters& p = requested_;
        Follow(block);
        const float* in_l = block.left;
        const float* in_r = block.right;
        // The Drive stage is skipped entirely once it has faded out, so that drive 0 % is
        // the plain filter without its auto level.
        drive_mix_.SetEnabled(p.drive > filter_fx::kDriveOffPercent);
        if(drive_mix_.Active()) {
            Drive::Parameters d;
            d.drive = p.drive;
            d.tone = 100.f;
            d.target_offset_db = 0.f;
            drive_.SetParameters(d);
            if(drive_.Process({block.left, block.right, storage_->left, storage_->right, block.frames})) {
                drive_mix_.Process(block.left, block.right, storage_->left, storage_->right, storage_->left,
                                   storage_->right, block.frames);
                in_l = storage_->left;
                in_r = storage_->right;
            }
        }
        const float frames = float(block.frames);
        if(snap_) {
            cutoff_ = p.cutoff;
            snap_ = false;
        }
        const float smooth = frames * (1000.f / (filter_fx::kCutoffSmoothMs * float(seed::dsp::filter::kSampleRate)));
        cutoff_ += (smooth < 1.f ? smooth : 1.f) * (p.cutoff - cutoff_);
        const float lfo_step = p.lfo_rate * frames / float(seed::dsp::filter::kSampleRate);
        const float modulation = p.env_amount * EnvelopePosition();
        seed::dsp::filter::Parameters f;
        f.resonance = p.resonance * 0.01f;
        f.bass_comp = float(unsigned(p.bass_comp)) * 0.5f;
        f.mode = p.mode;
        for(unsigned c = 0; c < 2; ++c) {
            const float lfo = Lfo(c, lfo_step, p);
            note_[c] = cutoff_ + p.lfo_depth * lfo + modulation;
            f.cutoff_note = note_[c];
            filter_[c].Set(f);
        }
        filter_[0].Process(in_l, block.out_left, block.frames);
        filter_[1].Process(in_r, block.out_right, block.frames);
        for(std::size_t n = 0; n < block.frames; ++n) {
            block.out_left[n] = filter_fx::WetCeiling(block.out_left[n]);
            block.out_right[n] = filter_fx::WetCeiling(block.out_right[n]);
        }
        return true;
    }

  private:
    static Parameters Sanitize(Parameters p) {
        const auto clamp = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
        p.mode = unsigned(p.mode) < seed::dsp::filter::kModeCount ? p.mode : filter_fx::Mode::Lp24;
        p.cutoff = clamp(p.cutoff, 24.f, 135.f);
        p.resonance = clamp(p.resonance, 0.f, 100.f);
        p.drive = clamp(p.drive, 0.f, 100.f);
        p.mix = clamp(p.mix, 0.f, 100.f);
        p.lfo_rate = clamp(p.lfo_rate, 0.05f, 20.f);
        p.lfo_depth = clamp(p.lfo_depth, 0.f, 48.f);
        p.lfo_shape = filter_fx::LfoShape(unsigned(p.lfo_shape) & 3);
        p.env_amount = clamp(p.env_amount, -48.f, 48.f);
        p.env_release = clamp(p.env_release, 10.f, 2000.f);
        p.bass_comp = unsigned(p.bass_comp) > 2 ? filter_fx::BassComp::Full : p.bass_comp;
        return p;
    }

    // Peak follower of the dry input, before the drive: fast attack, adjustable release.
    void Follow(const audio::StereoBlock& block) {
        const float release = seed::dsp::SmoothingCoefficient(requested_.env_release, float(seed::dsp::filter::kSampleRate));
        float e = envelope_;
        for(std::size_t n = 0; n < block.frames; ++n) {
            float l = block.left[n] < 0.f ? -block.left[n] : block.left[n];
            float r = block.right[n] < 0.f ? -block.right[n] : block.right[n];
            float peak = l > r ? l : r;
            peak = peak < 1.f ? peak : 1.f; // also catches inf; NaN compares false and counts as 1
            e += (peak > e ? attack_k_ : release) * (peak - e);
        }
        envelope_ = e > 1e-9f ? e : 0.f; // stops the decay above the denormal range
    }
    // 0 at kEnvFloorDb and below, 1 at 0 dBFS: linear in dB, so quiet playing also moves it.
    float EnvelopePosition() const {
        const float t = 1.f - seed::dsp::GainToDb(envelope_ + 1e-9f) * (1.f / filter_fx::kEnvFloorDb);
        return t > 0.f ? (t < 1.f ? t : 1.f) : 0.f;
    }

    // -1..1 at the end of this block. The core ramps the cutoff between block ends.
    float Lfo(unsigned channel, float step, const Parameters& p) {
        float phase = phase_[channel] + step;
        const bool wrapped = phase >= 1.f;
        phase -= wrapped ? 1.f : 0.f;
        phase_[channel] = phase;
        // The left channel leads; without stereo the right one is its copy.
        if(channel == 1 && !p.lfo_stereo)
            phase = phase_[0];
        switch(p.lfo_shape) {
            case filter_fx::LfoShape::Triangle: return mod::ShapeValue(mod::Triangle, phase);
            case filter_fx::LfoShape::Saw: return mod::ShapeValue(mod::SawUp, phase);
            case filter_fx::LfoShape::SampleHold:
                if(wrapped)
                    hold_[channel] = rng_.Bipolar();
                return p.lfo_stereo ? hold_[channel] : hold_[0];
            default: return mod::ShapeValue(mod::Sine, phase);
        }
    }

    audio::Config config_;
    Parameters requested_;
    Storage* storage_ = nullptr;
    Drive drive_;
    DriveMixer drive_mix_;
    seed::dsp::filter::Ladder filter_[2] = {seed::dsp::filter::Ladder(0x2545F491u), seed::dsp::filter::Ladder(0x9E3779B9u)};
    mod::Rng rng_;
    float cutoff_ = 100.f, note_[2] = {100.f, 100.f};
    float phase_[2] = {0.f, 0.25f}, hold_[2] = {0.f, 0.f};
    float envelope_ = 0.f, attack_k_ = 0.f;
    bool snap_ = true;
};

} // namespace seed::fx
