#pragma once
// Mod on Daisy targets of the separate Physical Modeling build (MOD=1, see
// docs/MODULATION.md). The grid columns are ParameterManager parameters 0..7.
#include "parameter_manager.h"
#include "version.h"
#include "mod_on_daisy/types.h"

namespace physical_modeling {
static_assert(Frequency == 0 && Structure == 1 && Brightness == 2 && Damping == 3 && Position == 4
                  && Excitation == 5 && Decay == 6 && Wet == 7,
              "grid column t is parameter t");

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "physical-modeling";
    info.version = PHYSICAL_MODELING_VERSION "-mod";
    info.targets[Frequency] = {"Pitch", "+-12 st", 0.25f, true, false}; // 0..1 = 48 semitones
    info.targets[Structure] = {"Structure", "100%", 1.0f, true, false};
    info.targets[Brightness] = {"Brightness", "100%", 1.0f, true, false};
    info.targets[Damping] = {"Damping", "100%", 1.0f, true, false};
    info.targets[Position] = {"Position", "100%", 1.0f, true, false};
    info.targets[Excitation] = {"Excitation", "100%", 1.0f, false, true}; // latched at the strike
    info.targets[Decay] = {"Reverb", "100%", 1.0f, false, false};
    info.targets[Wet] = {"Dry/Wet", "100%", 1.0f, false, false};
    return info;
}
}
