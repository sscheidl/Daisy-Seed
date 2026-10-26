#pragma once
// SPDX-License-Identifier: MIT
// Acid 303 parameters (contracts/parameters.json) <-> Voice Params, and the CC plan
// (PARAMETERS.md): CC 0..127 linear or logarithmic over min..max, enum index
// floor(cc * count / 128), bool cc >= 64. Control path only (uses libm).
#include "step_seq/param_table.h"
#include "voice.h"
#include <cmath>

namespace acid303 {

inline const sq::ComponentParams& Contract() { return sq::kAcid303Component; }

inline bool ReadParam(const Params& p, uint16_t id, int32_t& out) {
    const auto milli = [](double v) { return int32_t(std::lround(v * 1000.0)); };
    switch(id) {
        case 1: out = milli(p.waveform); return true;
        case 2: out = milli(p.tune); return true;
        case 3: out = milli(p.fine); return true;
        case 4: out = milli(p.cutoff); return true;
        case 5: out = milli(p.resonance); return true;
        case 6: out = milli(p.env_mod); return true;
        case 7: out = milli(p.decay); return true;
        case 8: out = milli(p.accent); return true;
        case 9: out = milli(p.volume); return true;
        case 10: out = milli(p.slide_ms); return true;
        case 11: out = milli(int(p.slide_mode)); return true;
        default: return false;
    }
}

inline bool WriteParam(Params& p, uint16_t id, int32_t value) {
    const sq::ParamDesc* d = sq::FindParam(Contract(), id);
    if(!d || (d->flags & sq::FlagReadonly))
        return false;
    value = sq::Constrain(*d, value);
    const float f = float(value) / 1000.f;
    const int i = int(value / 1000);
    switch(id) {
        case 1: p.waveform = i; break;
        case 2: p.tune = i; break;
        case 3: p.fine = f; break;
        case 4: p.cutoff = f; break;
        case 5: p.resonance = f; break;
        case 6: p.env_mod = f; break;
        case 7: p.decay = f; break;
        case 8: p.accent = f; break;
        case 9: p.volume = f; break;
        case 10: p.slide_ms = f; break;
        case 11: p.slide_mode = SlideMode(i); break;
        default: return false;
    }
    return true;
}

// Wire value (milli-units) of a CC value for a parameter.
inline int32_t FromCc(const sq::ParamDesc& d, uint8_t cc) {
    if(cc > 127)
        cc = 127;
    const double lo = d.min / 1000.0, hi = d.max / 1000.0, x = cc / 127.0;
    double v;
    if(d.type == sq::TypeBool)
        v = cc >= 64 ? 1 : 0;
    else if(d.type == sq::TypeEnum)
        v = std::floor(cc * double(d.enum_count) / 128.0);
    else if(d.flags & sq::FlagLog)
        v = lo * std::pow(hi / lo, x);
    else
        v = lo + (hi - lo) * x;
    return sq::Constrain(d, int32_t(std::lround(v * 1000.0)));
}

// Parameter that a CC number controls in a component, or nullptr.
inline const sq::ParamDesc* ParamOfCc(const sq::ComponentParams& c, uint8_t cc) {
    for(uint8_t i = 0; i < c.count; ++i)
        if(c.params[i].cc == cc)
            return &c.params[i];
    return nullptr;
}

} // namespace acid303
