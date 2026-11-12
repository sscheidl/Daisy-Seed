#pragma once
// Mod on Daisy targets of Toy Piano v0.3 (modules/mod-on-daisy, ARCHITECTURE § 6.3).
// Toy Piano is the development environment for the matrix: columns 5-7 stay free
// for experiments and are greyed out in the editor.
#include "config.h"
#include "version.h"
#if TOY_PIANO_FX || TOY_PIANO_TAPE || TOY_PIANO_FV1 || TOY_PIANO_FILTER || TOY_PIANO_CUMULONIMBUS
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
#if TOY_PIANO_CUMULONIMBUS
// Columns 5-7: grain position, grain mix, hall mix (bases from the FX page). Modes report the SW17
// page and the SW18 state for the extended monitor.
constexpr unsigned ModGrainPosition = 5, ModGrainMix = 6, ModHallMix = 7;
inline constexpr mod::ModeInfo kCumuloModes[] = {
    {"Piano Run", "SW17 Piano / SW18 centre"}, {"Piano Freeze", "SW17 Piano / SW18 up"},
    {"Piano Bypass", "SW17 Piano / SW18 down"}, {"FX Run", "SW17 FX / SW18 centre"},
    {"FX Freeze", "SW17 FX / SW18 up"}, {"FX Bypass", "SW17 FX / SW18 down"},
};
#endif
#if TOY_PIANO_FILTER
// Age stays a direct control on VR8, not a matrix target (owner decision 2026-11-06).
constexpr unsigned ModCutoff = 5, ModResonance = 6, ModDrive = 7;
constexpr float kModCutoffSpan = 48.0f / 111.0f; // depth 100 = +-48 semitones of the cutoff range
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
#elif TOY_PIANO_TAPE
    // The five Toy Piano columns; the tape is played from the pots only.
    info.key = "toy-piano-tape";
    info.version = TOY_PIANO_TAPE_VERSION;
#elif TOY_PIANO_FV1
    // The five Toy Piano columns (VR2 attack, VR3 decay; VR1 transposes); the FV-1 program uses VR4..VR8 and MIDI.
    info.key = "toy-piano-fv1";
    info.version = TOY_PIANO_FV1_VERSION;
#elif TOY_PIANO_FILTER
    info.key = "toy-piano-filter";
    info.version = TOY_PIANO_FILTER_VERSION;
    info.targets[ModCutoff] = {"Cutoff", "+-48 st", kModCutoffSpan, true, false};
    info.targets[ModResonance] = {"Resonance", "100%", 1.f, false, false};
    info.targets[ModDrive] = {"Drive", "100%", 1.f, false, false};
#elif TOY_PIANO_CUMULONIMBUS
    info.key = "toy-piano-cumulonimbus";
    info.version = TOY_PIANO_CUMULONIMBUS_VERSION;
    info.modes = kCumuloModes;
    info.mode_count = 6;
    info.targets[ModGrainPosition] = {"GrainPos", "100%", 1.f, false, false};
    info.targets[ModGrainMix] = {"GrainMix", "100%", 1.f, false, false};
    info.targets[ModHallMix] = {"HallMix", "100%", 1.f, false, false};
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
