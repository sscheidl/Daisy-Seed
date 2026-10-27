#pragma once
// SPDX-License-Identifier: MIT
// Stereo Delay parameters (contracts/parameters.json, component stereo-delay) <-> Parameters.
#include "../../../../sequencers/step-seq/include/step_seq/param_table.h"
#include "delay.h"

namespace seed::fx::delay {

inline bool ReadParam(const Parameters& p, uint16_t id, int32_t& out) {
    const auto milli = [](float v) { return int32_t(v * 1000.f + (v < 0 ? -0.5f : 0.5f)); };
    switch(id) {
        case 1: out = p.enabled ? 1000 : 0; return true;
        case 2: out = int32_t(p.mode) * 1000; return true;
        case 3: out = milli(p.time_ms); return true;
        case 4: out = p.sync ? 1000 : 0; return true;
        case 5: out = p.note_value * 1000; return true;
        case 6: out = milli(p.feedback); return true;
        case 7: out = milli(p.mix); return true;
        case 8: out = milli(p.damping_hz); return true;
        default: return false;
    }
}

inline bool WriteParam(Parameters& p, uint16_t id, int32_t value) {
    const sq::ParamDesc* d = sq::FindParam(sq::kStereoDelayComponent, id);
    if(!d)
        return false;
    value = sq::Constrain(*d, value);
    const float f = float(value) / 1000.f;
    switch(id) {
        case 1: p.enabled = value != 0; break;
        case 2: p.mode = Mode(value / 1000); break;
        case 3: p.time_ms = f; break;
        case 4: p.sync = value != 0; break;
        case 5: p.note_value = int(value / 1000); break;
        case 6: p.feedback = f; break;
        case 7: p.mix = f; break;
        case 8: p.damping_hz = f; break;
        default: return false;
    }
    return true;
}

} // namespace seed::fx::delay
