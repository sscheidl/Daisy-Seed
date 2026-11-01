#pragma once
// SPDX-License-Identifier: MIT
// One parameter register for kit pots/keys, MIDI CC, the SQ protocol (control app) and the
// modulation matrix (MODULAR_ARCHITECTURE.md). Wire values are the SQ milli-units of the
// generated tables (contracts/parameters.json); this file converts them to the typed
// parameters of the instrument, the FX and the product. Host-tested, no libDaisy.
#include "streichwurst/param_table.h"
#include "streichwurst/params.h"
#include "streichwurst_fx/params.h"
#include <cmath>
#include <cstdint>

namespace streichwurst::product {

// SQ component indices (IDENTIFY order).
enum Component : uint8_t { kInstrument = 0, kFx = 1, kProduct = 2, kComponents = 3 };

inline const sq::ComponentParams& Table(uint8_t component) {
    static const sq::ComponentParams* const kTables[kComponents] = {
        &sq::kStreichwurstInstrumentComponent, &sq::kStreichwurstFxComponent, &sq::kStreichwurstComponent};
    return *kTables[component < kComponents ? component : 0];
}

// Parameter IDs (stable, contracts/parameters.json).
namespace id {
enum Instrument : uint16_t {
    Registration = 1, Octave, Crescendo, Release, SoloTone, SoloAttack, SoloRelease, SoloSustainMode, SoloSustain,
    SoloTremDepth, SoloTremRate, SplitMode, SplitKey, StringVelocity, SoloVelocity, BendRange, Transpose, Tune
};
enum Fx : uint16_t { Animate = 1, AnimateRate, EnsembleOn, EnsembleMode, Phaser, PhaserRate, Reverb, ReverbDecay };
enum Product : uint16_t { Balance = 1, Master, MidiChannel, PotPage, Preset };
} // namespace id

struct ProductParams {
    float balance = 0.5f;  // 0..1 (SF-F07 shape)
    float master = 0.8f;   // 0..1
    int midi_channel = 0;  // 0 = omni, 1..16
};

// The complete register state (what the app, the pots and MIDI see).
struct State {
    InstrumentParams inst;
    streichwurst_fx::FxParams fx;
    ProductParams prod;
    int pot_page = 0; // read only for the protocol: kit state
    int preset = 0;   // read only: last recalled RAM preset
};

inline float Percent(int32_t milli) { return float(milli) / 100000.f; }
inline int32_t FromPercent(float v) { return int32_t(std::lround(double(v) * 100000.0)); }
inline float Units(int32_t milli) { return float(milli) / 1000.f; }
inline int32_t FromUnits(float v) { return int32_t(std::lround(double(v) * 1000.0)); }
inline int Whole(int32_t milli) { return int((milli >= 0 ? milli + 500 : milli - 500) / 1000); }

// Wire value of a parameter; false for an unknown ID.
inline bool ReadParam(const State& s, uint8_t component, uint16_t pid, int32_t& out) {
    using namespace id;
    const InstrumentParams& i = s.inst;
    const streichwurst_fx::FxParams& f = s.fx;
    switch(component) {
        case kInstrument:
            switch(pid) {
                case Registration: out = FromPercent(i.string_registration); return true;
                case Octave: out = i.string_octave * 1000; return true;
                case Crescendo: out = FromPercent(i.string_crescendo); return true;
                case Release: out = FromPercent(i.string_release); return true;
                case SoloTone: out = FromPercent(i.solo_tone); return true;
                case SoloAttack: out = FromPercent(i.solo_attack); return true;
                case SoloRelease: out = FromPercent(i.solo_release); return true;
                case SoloSustainMode: out = i.solo_sustain_mode * 1000; return true;
                case SoloSustain: out = FromPercent(i.solo_sustain_level); return true;
                case SoloTremDepth: out = FromPercent(i.solo_tremolo_depth); return true;
                case SoloTremRate: out = FromUnits(i.solo_tremolo_rate_hz); return true;
                case SplitMode: out = i.split_mode * 1000; return true;
                case SplitKey: out = i.split_key * 1000; return true;
                case StringVelocity: out = FromPercent(i.string_velocity); return true;
                case SoloVelocity: out = FromPercent(i.solo_velocity); return true;
                case BendRange: out = i.bend_range * 1000; return true;
                case Transpose: out = i.transpose * 1000; return true;
                case Tune: out = FromUnits(i.tune_cents); return true;
                default: return false;
            }
        case kFx:
            switch(pid) {
                case Animate: out = FromPercent(f.animate_amount); return true;
                case AnimateRate: out = FromUnits(f.animate_rate_hz); return true;
                case EnsembleOn: out = f.ensemble_on ? 1000 : 0; return true;
                case EnsembleMode: out = f.ensemble_mode * 1000; return true;
                case Phaser: out = FromPercent(f.phaser_amount); return true;
                case PhaserRate: out = FromUnits(f.phaser_rate_hz); return true;
                case Reverb: out = FromPercent(f.reverb_wet); return true;
                case ReverbDecay: out = FromPercent(f.reverb_decay); return true;
                default: return false;
            }
        case kProduct:
            switch(pid) {
                case Balance: out = FromPercent(s.prod.balance); return true;
                case Master: out = FromPercent(s.prod.master); return true;
                case MidiChannel: out = s.prod.midi_channel * 1000; return true;
                case PotPage: out = s.pot_page * 1000; return true;
                case Preset: out = s.preset * 1000; return true;
                default: return false;
            }
        default: return false;
    }
}

// Writes a wire value, clamped and rasterized by the descriptor first. Read-only
// parameters are refused unless `device` is set (the kit's own state). False: unknown.
inline bool WriteParam(State& s, uint8_t component, uint16_t pid, int32_t value, bool device = false) {
    using namespace id;
    if(component >= kComponents)
        return false;
    const sq::ParamDesc* d = sq::FindParam(Table(component), pid);
    if(!d || ((d->flags & sq::FlagReadonly) && !device))
        return false;
    value = sq::Constrain(*d, value);
    InstrumentParams& i = s.inst;
    streichwurst_fx::FxParams& f = s.fx;
    switch(component) {
        case kInstrument:
            switch(pid) {
                case Registration: i.string_registration = Percent(value); break;
                case Octave: i.string_octave = Whole(value); break;
                case Crescendo: i.string_crescendo = Percent(value); break;
                case Release: i.string_release = Percent(value); break;
                case SoloTone: i.solo_tone = Percent(value); break;
                case SoloAttack: i.solo_attack = Percent(value); break;
                case SoloRelease: i.solo_release = Percent(value); break;
                case SoloSustainMode: i.solo_sustain_mode = Whole(value); break;
                case SoloSustain: i.solo_sustain_level = Percent(value); break;
                case SoloTremDepth: i.solo_tremolo_depth = Percent(value); break;
                case SoloTremRate: i.solo_tremolo_rate_hz = Units(value); break;
                case SplitMode: i.split_mode = Whole(value); break;
                case SplitKey: i.split_key = Whole(value); break;
                case StringVelocity: i.string_velocity = Percent(value); break;
                case SoloVelocity: i.solo_velocity = Percent(value); break;
                case BendRange: i.bend_range = Whole(value); break;
                case Transpose: i.transpose = Whole(value); break;
                case Tune: i.tune_cents = Units(value); break;
                default: return false;
            }
            return true;
        case kFx:
            switch(pid) {
                case Animate: f.animate_amount = Percent(value); break;
                case AnimateRate: f.animate_rate_hz = Units(value); break;
                case EnsembleOn: f.ensemble_on = value >= 500; break;
                case EnsembleMode: f.ensemble_mode = Whole(value); break;
                case Phaser: f.phaser_amount = Percent(value); break;
                case PhaserRate: f.phaser_rate_hz = Units(value); break;
                case Reverb: f.reverb_wet = Percent(value); break;
                case ReverbDecay: f.reverb_decay = Percent(value); break;
                default: return false;
            }
            return true;
        default:
            switch(pid) {
                case Balance: s.prod.balance = Percent(value); break;
                case Master: s.prod.master = Percent(value); break;
                case MidiChannel: s.prod.midi_channel = Whole(value); break;
                case PotPage: s.pot_page = Whole(value); break;
                case Preset: s.preset = Whole(value); break;
                default: return false;
            }
            return true;
    }
}

// Normalized 0..1 position of a wire value inside the descriptor range (pots, matrix,
// soft takeover). Logarithmic descriptors use a log scale.
inline float Normalized(const sq::ParamDesc& d, int32_t value) {
    if(d.max <= d.min)
        return 0.f;
    if((d.flags & sq::FlagLog) && d.min > 0) {
        const float v = float(value < d.min ? d.min : (value > d.max ? d.max : value));
        return std::log(v / float(d.min)) / std::log(float(d.max) / float(d.min));
    }
    const float n = float(value - d.min) / float(d.max - d.min);
    return n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
}
inline int32_t FromNormalized(const sq::ParamDesc& d, float n) {
    if(!std::isfinite(n))
        n = 0.f;
    n = n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
    double v;
    if((d.flags & sq::FlagLog) && d.min > 0)
        v = double(d.min) * std::pow(double(d.max) / double(d.min), double(n));
    else if(d.type == sq::TypeEnum || d.type == sq::TypeInt || d.type == sq::TypeBool) {
        const int count = (d.max - d.min) / 1000 + 1; // equal zones per value (pots, CC plan)
        int k = int(n * float(count));
        if(k >= count)
            k = count - 1;
        v = double(d.min) + 1000.0 * k;
    } else
        v = double(d.min) + (double(d.max) - double(d.min)) * double(n);
    return sq::Constrain(d, int32_t(std::lround(v)));
}

// CC plan (PARAMETERS.md rule shared with Acid Seq): continuous linear/log over the
// range, enum/int index = floor(cc * count / 128), bool cc >= 64.
inline int32_t FromCc(const sq::ParamDesc& d, uint8_t cc) {
    if(d.type == sq::TypeBool)
        return cc >= 64 ? d.max : d.min;
    if(d.type == sq::TypeEnum || d.type == sq::TypeInt) {
        const int count = (d.max - d.min) / 1000 + 1;
        return d.min + 1000 * (int(cc) * count / 128);
    }
    return FromNormalized(d, float(cc) / 127.f);
}
struct CcTarget {
    uint8_t component = 0;
    const sq::ParamDesc* desc = nullptr;
};
inline CcTarget ParamOfCc(uint8_t cc) {
    for(uint8_t c = 0; c < kComponents; ++c)
        for(uint8_t i = 0; i < Table(c).count; ++i)
            if(Table(c).params[i].cc == cc && !(Table(c).params[i].flags & sq::FlagReadonly))
                return {c, &Table(c).params[i]};
    return {};
}

// Own RAM presets (S15). Defaults of our own design, no manufacturer presets.
inline State Preset(int index) {
    State s;
    switch(index & 3) {
        case 1: // dark ensemble strings
            s.inst.string_registration = 0.15f;
            s.inst.string_crescendo = 0.45f;
            s.inst.string_release = 0.6f;
            s.fx.ensemble_mode = 1;
            s.fx.reverb_wet = 0.35f;
            break;
        case 2: // bright thin strings, both octaves, phaser
            s.inst.string_registration = 0.85f;
            s.inst.string_octave = OctaveBoth;
            s.fx.ensemble_mode = 2;
            s.fx.phaser_amount = 0.4f;
            s.fx.animate_amount = 0.3f;
            break;
        case 3: // split: strings below C4, solo lead above
            s.inst.split_mode = SplitSplit;
            s.inst.split_key = 60;
            s.inst.solo_tone = 0.75f;
            s.inst.solo_tremolo_depth = 0.25f;
            s.fx.ensemble_mode = 0;
            break;
        default: break; // 0: defaults of the contract
    }
    s.preset = index & 3;
    return s;
}

// Mod on Daisy matrix columns (own selection; every parameter stays reachable through
// SQ/CC regardless of these eight). Base value 0..1 in, effective value out.
enum MatrixTarget : uint8_t {
    TargetRegistration, TargetAnimate, TargetPhaser, TargetReverb, TargetBalance, TargetSoloTone, TargetTremolo,
    TargetPitch, kMatrixTargets
};
constexpr float kPitchModSemitones = 2.f; // depth 100 with span 0.5 -> +-1 semitone

} // namespace streichwurst::product
