// SPDX-License-Identifier: MIT
// Mod on Daisy targets of the separate Looper build (MOD=1, LOOPER_MOD, see
// docs/MODULATION.md). Header-only: the firmware, the host tests and the module's
// simulator read the same column table. The looper is not polyphonic, so every
// column is global and none is latched.
#pragma once
#include "looper_engine.h"
#include "version.h"
#include "mod_on_daisy/engine.h"
#include "mod_on_daisy/types.h"
#include <algorithm>

namespace looper::columns {

// Grid columns 0-7. Speed, Start, Size, Slice, Skip, Inertia and Decay modulate
// the value the pot or its CC sets; Mix has no pot and sits at kMixBase.
enum Column : unsigned { Speed, Start, Size, Slice, Skip, Inertia, Decay, Mix, kColumns };
static_assert(kColumns == mod::kTargets, "one looper control per grid column");

constexpr float kSpeedRange = kMaxSemitones - kMinSemitones;  // 60 semitones over 0..1
constexpr float kSpeedSpan = 12.f / kSpeedRange;              // depth 100 = +-12 semitones
constexpr float kMixBase = .5f;                               // dry and loop both at unity

// Varispeed in semitones (-24..+36) as the column's normalized position.
inline float SpeedToUnit(float semitones) { return (semitones - kMinSemitones) / kSpeedRange; }

// Mix: 0 = dry input only, kMixBase = dry and loop at unity, 1 = loop only.
struct MixGains {
    float dry = 1.f, loop = 1.f;
};
inline MixGains MixFromUnit(float x) {
    return {std::min(1.f, 2.f * (1.f - x)), std::min(1.f, 2.f * x)};
}

// The controls before modulation: the pot or, after a CC took the knob over,
// the CC value (main.cpp). Varispeed in whole semitones, the others 0..1.
struct Bases {
    float semitones = 0.f, start = 0.f, size = 1.f, slice = 0.f, skip = 0.f, inertia = 0.f,
          decay = kDefaultDecay;
};
// What the engine and the output stage receive.
struct Values {
    float semitones = 0.f, start = 0.f, size = 1.f, slice = 0.f, skip = 0.f, inertia = 0.f,
          decay = kDefaultDecay;
    MixGains mix;
};

// Base value + modulation, clamped to the column's range (Engine::Apply). With no
// active connection every value is its base bit for bit. Speed adds the modulation
// unrounded to the whole-semitone base, so an LFO bends smoothly.
inline Values Modulate(const mod::Engine& engine, const Bases& b) {
    Values v;
    const float speed = SpeedToUnit(b.semitones);
    v.semitones = b.semitones + kSpeedRange * (engine.Apply(Speed, speed) - speed);
    v.start = engine.Apply(Start, b.start);
    v.size = engine.Apply(Size, b.size);
    v.slice = engine.Apply(Slice, b.slice);
    v.skip = engine.Apply(Skip, b.skip);
    v.inertia = engine.Apply(Inertia, b.inertia);
    v.decay = engine.Apply(Decay, b.decay);
    v.mix = MixFromUnit(engine.Apply(Mix, kMixBase));
    return v;
}

#if MOD_ON_DAISY_EXTENSIONS
// Column positions before modulation, for the editor's monitor (PROTOCOL.md § 3.3).
inline void ReportBases(mod::Engine& engine, const Bases& b) {
    engine.ReportBase(Speed, SpeedToUnit(b.semitones));
    engine.ReportBase(Start, b.start);
    engine.ReportBase(Size, b.size);
    engine.ReportBase(Slice, b.slice);
    engine.ReportBase(Skip, b.skip);
    engine.ReportBase(Inertia, b.inertia);
    engine.ReportBase(Decay, b.decay);
    engine.ReportBase(Mix, kMixBase);
}

// The four loop modes in LoopMode order, reported to the editor (PROTOCOL.md § 3.6).
// 7-bit ASCII only (protocol strings).
inline constexpr mod::ModeInfo kModes[] = {
    {"Sound on Sound", "S9 oder CC110"},
    {"Replace", "S10 oder CC111"},
    {"Frippertronics", "S11 oder CC112"},
    {"Resample", "S12 oder CC113"},
};
#endif

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "looper";
    info.version = LOOPER_VERSION;  // one version for image, marker and IDENTIFY
#if MOD_ON_DAISY_EXTENSIONS
    info.modes = kModes;
    info.mode_count = uint8_t(sizeof(kModes) / sizeof(kModes[0]));
#endif
    info.targets[Speed] = {"Speed", "+-12 st", kSpeedSpan, false, false};
    info.targets[Start] = {"Start", "100%", 1.f, false, false};
    info.targets[Size] = {"Size", "100%", 1.f, false, false};
    info.targets[Slice] = {"Slice", "100%", 1.f, false, false};
    info.targets[Skip] = {"Skip", "100%", 1.f, false, false};
    info.targets[Inertia] = {"Inertia", "100%", 1.f, false, false};
    info.targets[Decay] = {"Fripp Decay", "100%", 1.f, false, false};
    info.targets[Mix] = {"Mix", "+-50%", .5f, false, false};
    return info;
}

}  // namespace looper::columns
