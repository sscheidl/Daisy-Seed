#pragma once
// Column tables the simulator can play. Only the "Simulator" ports use them; with a
// real device the editor reads everything from the device (GUI.md § 1).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal" // firmware headers, built with other warnings
#endif
#include "../../../../instruments/physical-modeling/src/modulation.h"
#include "../../../../instruments/toy-piano/src/modulation.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "mod_on_daisy/module.h"
#include <array>

namespace editor {
struct SimInstrument {
    const char* label;
    mod::InstrumentInfo (*make)();
    std::array<float, mod::kTargets> bases; // reported pot positions; -1 = the firmware reports none
    mod::Extensions extensions;             // as the firmware's InitModulation enables them
    int mode;                               // reported instrument mode; -1 = none
};
// Toy Piano v0.3 reports no base values and has no extensions; Physical Modeling MOD=1
// (mod4) has all: here its power-on pot defaults (ParameterManager), Level at unity and
// the model of bank A, SW18 up (Modal Resonator).
inline const SimInstrument kSimInstruments[] = {
    {"Simulator: Toy Piano", toy_piano::MakeModInfo, {-1, -1, -1, -1, -1, -1, -1, -1}, {false, false, false}, -1},
    {"Simulator: Physical Modeling", physical_modeling::MakeModInfo,
     {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.8f, physical_modeling::kLevelBase, 0.25f}, {true, true, true}, 0},
};
}
