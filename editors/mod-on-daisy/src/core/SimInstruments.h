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

namespace editor {
struct SimInstrument {
    const char* label;
    mod::InstrumentInfo (*make)();
};
inline const SimInstrument kSimInstruments[] = {
    {"Simulator: Toy Piano", toy_piano::MakeModInfo},
    {"Simulator: Physical Modeling", physical_modeling::MakeModInfo},
};
}
