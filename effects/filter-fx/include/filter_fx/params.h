#pragma once
// SPDX-License-Identifier: MIT
// Filter FX parameters (contracts/parameters.json, component filter-fx) <-> Parameters.
#include "filter_fx.h"
#include "param_table.h"

namespace seed::fx::filter_fx {

inline bool ReadParam(const Parameters& p, uint16_t id, int32_t& out) {
    const auto milli = [](float v) { return int32_t(v * 1000.f + (v < 0 ? -0.5f : 0.5f)); };
    switch(id) {
        case 1: out = p.enabled ? 1000 : 0; return true;
        case 2: out = int32_t(p.mode) * 1000; return true;
        case 3: out = milli(p.cutoff); return true;
        case 4: out = milli(p.resonance); return true;
        case 5: out = milli(p.drive); return true;
        case 6: out = milli(p.mix); return true;
        case 7: out = milli(p.lfo_rate); return true;
        case 8: out = milli(p.lfo_depth); return true;
        case 9: out = int32_t(p.lfo_shape) * 1000; return true;
        case 10: out = p.lfo_stereo ? 1000 : 0; return true;
        case 11: out = milli(p.env_amount); return true;
        case 12: out = milli(p.env_release); return true;
        case 13: out = int32_t(p.bass_comp) * 1000; return true;
        default: return false;
    }
}

inline bool WriteParam(Parameters& p, uint16_t id, int32_t value) {
    const sq::ParamDesc* d = sq::FindParam(sq::kFilterFxComponent, id);
    if(!d)
        return false;
    value = sq::Constrain(*d, value);
    const float f = float(value) / 1000.f;
    switch(id) {
        case 1: p.enabled = value != 0; break;
        case 2: p.mode = Mode(value / 1000); break;
        case 3: p.cutoff = f; break;
        case 4: p.resonance = f; break;
        case 5: p.drive = f; break;
        case 6: p.mix = f; break;
        case 7: p.lfo_rate = f; break;
        case 8: p.lfo_depth = f; break;
        case 9: p.lfo_shape = LfoShape(value / 1000); break;
        case 10: p.lfo_stereo = value != 0; break;
        case 11: p.env_amount = f; break;
        case 12: p.env_release = f; break;
        case 13: p.bass_comp = BassComp(value / 1000); break;
        default: return false;
    }
    return true;
}

} // namespace seed::fx::filter_fx
