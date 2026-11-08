// Mod on Daisy targets of the separate Nebulae build (MOD=1, NEBULAE_MOD, see
// docs/MODULATION.md). Header-only: the firmware, the host tests and the module's
// simulator read the same column table. Nebulae has one shared stereo voice, so every
// column is global and none is latched.
#pragma once
#include "control.h"
#include "version.h"
#include "mod_on_daisy/engine.h"
#include "mod_on_daisy/types.h"

namespace neb::columns {

// Grid columns 0-7 are the eight primaries in CC20-27 order: the column number is the
// neb::Param index, so one register serves pots, CCs and the matrix.
static_assert(Start == 0 && Window == 7 && Window + 1 == mod::kTargets, "one primary per grid column");

// One matrix tick is 48 frames at 48 kHz, the module's 1 kHz control rate.
constexpr std::size_t kTickFrames = 48;
static_assert(kRate / kTickFrames == 1000, "a tick must last exactly one millisecond");

// Depth 100 on Speed and Pitch moves the base by half the knob, so a full-depth
// source around the centre detent sweeps the whole range without resting at its ends.
constexpr float kCentredSpan = .5f;

// One value per column: offsets (timer -> audio) or bases (audio -> timer).
struct Frame {
    float value[mod::kTargets]{};
};

// Column positions before modulation, for the editor's monitor (PROTOCOL.md section 3.3).
inline void ReportBases(mod::Engine& engine, const Frame& bases) {
    for(std::size_t t = 0; t < mod::kTargets; ++t)
        engine.ReportBase(t, bases.value[t]);
}

inline mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "nebulae";
    info.version = NEBULAE_VERSION;  // one version for image, marker and IDENTIFY
    info.targets[Start] = {"Start", "100%", 1.f, false, false};
    info.targets[Speed] = {"Speed", "+-50%", kCentredSpan, false, false};
    info.targets[Size] = {"Size", "100%", 1.f, false, false};
    info.targets[Density] = {"Density", "100%", 1.f, false, false};
    info.targets[Pitch] = {"Pitch", "+-50%", kCentredSpan, false, false};
    info.targets[Overlap] = {"Overlap", "100%", 1.f, false, false};
    info.targets[Blend] = {"Blend", "100%", 1.f, false, false};
    info.targets[Window] = {"Window", "100%", 1.f, false, false};
    return info;
}

}  // namespace neb::columns
