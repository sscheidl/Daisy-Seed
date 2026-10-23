#pragma once
// Mod on Daisy targets of the separate Physical Modeling build (MOD=1, see
// docs/MODULATION.md). Grid columns 0-5 and 7 are ParameterManager parameters 0-5
// and 7; column 6 is the output level (the reverb decay it replaced had little effect).
#include "parameter_manager.h"
#include "version.h"
#include "mod_on_daisy/types.h"

namespace physical_modeling {
static_assert(Frequency == 0 && Structure == 1 && Brightness == 2 && Damping == 3 && Position == 4
                  && Excitation == 5 && Decay == 6 && Wet == 7,
              "grid columns 0-5 and 7 are the parameters of the same index");
constexpr unsigned kLevelColumn = 6;   // replaces Decay (reverb time), hardware test 2026-10-23
constexpr float kLevelBase = 0.5f;     // gain = 2 * x: 0..2 (+6 dB), 1 with an empty matrix
inline float LevelGain(float x) { return 2.0f * x; }

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "physical-modeling";
    info.version = PHYSICAL_MODELING_VERSION "-mod2"; // mod2: Level column, base values
    info.targets[Frequency] = {"Pitch", "+-12 st", 0.25f, true, false}; // 0..1 = 48 semitones
    info.targets[Structure] = {"Structure", "100%", 1.0f, true, false};
    info.targets[Brightness] = {"Brightness", "100%", 1.0f, true, false};
    info.targets[Damping] = {"Damping", "100%", 1.0f, true, false};
    info.targets[Position] = {"Position", "100%", 1.0f, true, false};
    info.targets[Excitation] = {"Excitation", "100%", 1.0f, false, true}; // latched at the strike
    info.targets[kLevelColumn] = {"Level", "+-100%", 0.5f, false, false};
    info.targets[Wet] = {"Dry/Wet", "100%", 1.0f, false, false};
    return info;
}
}
