#pragma once
// SPDX-License-Identifier: MIT
// Parameters of the Streichwurst instrument in physical/normalized units. The wire format
// (SQ milli-units, CCs, pots) lives in the product's parameter register; the instrument
// only sees these values. Invalid input (NaN/Inf, out of range) is clamped or replaced by
// the previous valid value before it reaches the DSP.
#include <cmath>

namespace streichwurst {

enum Octave : int { OctaveBase = 0, OctaveBoth = 1, OctaveUpper = 2 };
enum SoloSustainMode : int { SoloDecay = 0, SoloHold = 1, SoloPercussive = 2 };
enum SplitMode : int { SplitLayer = 0, SplitSplit = 1 };

struct InstrumentParams {
    // String section
    float string_registration = 0.5f; // 0..1, native registration byte 0..127 (SF-F03)
    int string_octave = OctaveBase;   // native octave value 0/1/2 (SF-F04)
    float string_crescendo = 0.2f;    // 0..1, native index 0..127 -> time constant (SF-F01/F02)
    float string_release = 0.45f;     // 0..1, same curve
    float string_velocity = 0.0f;     // 0..1, own: velocity influence on the string level
    // Soli (own replacement section, see FIDELITY.md)
    float solo_tone = 0.5f;
    float solo_attack = 0.1f;
    float solo_release = 0.3f;
    int solo_sustain_mode = SoloDecay;
    float solo_sustain_level = 0.7f;
    float solo_tremolo_depth = 0.0f;
    float solo_tremolo_rate_hz = 5.0f; // 0.5..12
    float solo_velocity = 1.0f;        // 0..1 velocity sensitivity
    // Keyboard
    int split_mode = SplitLayer;
    int split_key = 60;     // first key of the solo zone in split mode
    int bend_range = 2;     // semitones, 0..12
    int transpose = 0;      // semitones, -24..24
    float tune_cents = 0.f; // -50..50
};

namespace detail {
inline float Clamp(float v, float lo, float hi, float fallback) {
    if(!std::isfinite(v))
        return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace detail

// Valid copy of `in`; non-finite values keep the corresponding value of `previous`.
inline InstrumentParams Sanitize(const InstrumentParams& in, const InstrumentParams& previous) {
    using detail::Clamp;
    using detail::ClampInt;
    InstrumentParams p;
    p.string_registration = Clamp(in.string_registration, 0.f, 1.f, previous.string_registration);
    p.string_octave = ClampInt(in.string_octave, 0, 2);
    p.string_crescendo = Clamp(in.string_crescendo, 0.f, 1.f, previous.string_crescendo);
    p.string_release = Clamp(in.string_release, 0.f, 1.f, previous.string_release);
    p.string_velocity = Clamp(in.string_velocity, 0.f, 1.f, previous.string_velocity);
    p.solo_tone = Clamp(in.solo_tone, 0.f, 1.f, previous.solo_tone);
    p.solo_attack = Clamp(in.solo_attack, 0.f, 1.f, previous.solo_attack);
    p.solo_release = Clamp(in.solo_release, 0.f, 1.f, previous.solo_release);
    p.solo_sustain_mode = ClampInt(in.solo_sustain_mode, 0, 2);
    p.solo_sustain_level = Clamp(in.solo_sustain_level, 0.f, 1.f, previous.solo_sustain_level);
    p.solo_tremolo_depth = Clamp(in.solo_tremolo_depth, 0.f, 1.f, previous.solo_tremolo_depth);
    p.solo_tremolo_rate_hz = Clamp(in.solo_tremolo_rate_hz, 0.5f, 12.f, previous.solo_tremolo_rate_hz);
    p.solo_velocity = Clamp(in.solo_velocity, 0.f, 1.f, previous.solo_velocity);
    p.split_mode = ClampInt(in.split_mode, 0, 1);
    p.split_key = ClampInt(in.split_key, 0, 127);
    p.bend_range = ClampInt(in.bend_range, 0, 12);
    p.transpose = ClampInt(in.transpose, -24, 24);
    p.tune_cents = Clamp(in.tune_cents, -50.f, 50.f, previous.tune_cents);
    return p;
}

} // namespace streichwurst
