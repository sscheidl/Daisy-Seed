#pragma once
// SPDX-License-Identifier: MIT
// The eight Mod on Daisy columns of Streichwurst (own selection, docs/PARAMETERS.md). One
// source for the firmware and the editor's simulator. Every parameter stays reachable
// through SQ and MIDI CC; the matrix adds offsets to these eight only, at 1 kHz.
#include "mod_on_daisy/types.h"
#include "register.h"

namespace streichwurst::product {

inline mod::InstrumentInfo ModInfo(const char* version) {
    mod::InstrumentInfo info;
    info.key = "streichwurst";
    info.version = version;
    info.targets[TargetRegistration] = {"Registr.", "+-100%", 1.0f, false, false};
    info.targets[TargetAnimate] = {"Animate", "+-100%", 1.0f, false, false};
    info.targets[TargetPhaser] = {"Phaser", "+-100%", 1.0f, false, false};
    info.targets[TargetReverb] = {"Reverb", "+-100%", 1.0f, false, false};
    info.targets[TargetBalance] = {"Balance", "+-100%", 1.0f, false, false};
    info.targets[TargetSoloTone] = {"Solo Tone", "+-100%", 1.0f, false, false};
    info.targets[TargetTremolo] = {"Tremolo", "+-100%", 1.0f, false, false};
    info.targets[TargetPitch] = {"Pitch", "+-1 st", 0.5f, false, false};
    return info;
}

} // namespace streichwurst::product
