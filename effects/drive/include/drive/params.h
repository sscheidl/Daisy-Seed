#pragma once
// SPDX-License-Identifier: MIT
// Drive parameters (contracts/parameters.json, component drive) <-> Parameters.
#include "../../../../sequencers/step-seq/include/step_seq/param_table.h"
#include "drive.h"

namespace seed::fx::drive {

inline bool ReadParam(const Parameters& p, uint16_t id, int32_t& out) {
    const auto milli = [](float v) { return int32_t(v * 1000.f + (v < 0 ? -0.5f : 0.5f)); };
    switch(id) {
        case 1: out = p.enabled ? 1000 : 0; return true;
        case 2: out = int32_t(p.curve) * 1000; return true;
        case 3: out = milli(p.drive); return true;
        case 4: out = milli(p.tone); return true;
        case 5: out = milli(p.level_db); return true;
        case 6: out = p.auto_level ? 1000 : 0; return true;
        case 7: out = milli(p.compensation); return true;
        case 8: out = milli(p.target_offset_db); return true;
        default: return false;
    }
}

inline bool WriteParam(Parameters& p, uint16_t id, int32_t value) {
    const sq::ParamDesc* d = sq::FindParam(sq::kDriveComponent, id);
    if(!d)
        return false;
    value = sq::Constrain(*d, value);
    const float f = float(value) / 1000.f;
    switch(id) {
        case 1: p.enabled = value != 0; break;
        case 2: p.curve = Curve(value / 1000); break;
        case 3: p.drive = f; break;
        case 4: p.tone = f; break;
        case 5: p.level_db = f; break;
        case 6: p.auto_level = value != 0; break;
        case 7: p.compensation = f; break;
        case 8: p.target_offset_db = f; break;
        default: return false;
    }
    return true;
}

} // namespace seed::fx::drive
