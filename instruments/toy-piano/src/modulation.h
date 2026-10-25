#pragma once
// Mod on Daisy targets of Toy Piano v0.3 (modules/mod-on-daisy, ARCHITECTURE § 6.3).
// Toy Piano is the development environment for the matrix: columns 5-7 stay free
// for experiments and are greyed out in the editor.
#include "config.h"
#include "version.h"
#if TOY_PIANO_FX
#include "composed_version.h"
#endif
#include "mod_on_daisy/types.h"
#include <cmath>

namespace toy_piano {
enum ModTarget : uint8_t { ModPitch, ModLevel, ModHarmonic, ModAttack, ModDecay };
#if TOY_PIANO_FX
constexpr unsigned ModHallDecay = 5, ModHallMix = 6, ModHallX = 7;
inline constexpr mod::ModeInfo kFxModes[] = {
    {"Cloud", "A / SW18 up"}, {"Halo", "A / SW18 center"}, {"Scatter", "A / SW18 down"},
    {"Abyss", "B / SW18 up"}, {"Bloom", "B / SW18 center"}, {"Shimmer", "B / SW18 down"},
};
#endif

// Normalized bases. They map exactly to the v0.2 values (ratio 1, gain 1,
// harmonic 0.12), so an empty matrix leaves the sound bit-identical.
constexpr float kPitchBase = 0.5f;        // 0..1 = -12..+12 semitones
constexpr float kPitchRange = 24.0f;      // semitones over the normalized range
constexpr float kLevelBase = 0.5f;        // gain = 2 * x: 0..2 (+6 dB)
constexpr float kHarmonicScale = 0.5f;    // weight = 0.5 * x
constexpr float kHarmonicBase = kSecondHarmonic / kHarmonicScale;
static_assert(kHarmonicBase * kHarmonicScale == kSecondHarmonic, "harmonic base must map exactly");

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
#if TOY_PIANO_FX
    info.key = "toy-piano-ambient";
    info.version = TOY_PIANO_AMBIENT_VERSION;
    info.modes = kFxModes;
    info.mode_count = 6;
    info.targets[ModHallDecay] = {"HallDecay", "100%", 1.f, false, false};
    info.targets[ModHallMix] = {"HallMix", "100%", 1.f, false, false};
    info.targets[ModHallX] = {"HallX", "100%", 1.f, false, false};
#else
    info.key = "toy-piano";
    info.version = TOY_PIANO_VERSION;
#endif
    info.targets[ModPitch] = {"Pitch", "+-2 st", 2.0f / kPitchRange, true, false};
    info.targets[ModLevel] = {"Level", "+-100%", 0.5f, true, false};
    info.targets[ModHarmonic] = {"Harmonic", "+-0.5", 1.0f, true, false};
    info.targets[ModAttack] = {"Attack", "100%", 1.0f, false, true};
    info.targets[ModDecay] = {"Decay", "100%", 1.0f, false, true};
    return info;
}

inline float PitchRatio(float x) { return std::exp2((x - kPitchBase) * kPitchRange / 12.0f); }
inline float LevelGain(float x) { return 2.0f * x; }
inline float HarmonicWeight(float x) { return kHarmonicScale * x; }
}
