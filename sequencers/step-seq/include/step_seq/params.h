#pragma once
// SPDX-License-Identifier: MIT
// Step Seq parameters (contracts/parameters.json) <-> engine Settings. Values in
// milli-units as on the wire; writes are clamped and rounded to the parameter type.
#include "engine.h"
#include "param_table.h"
#include <cmath>

namespace step_seq {

using sq::Constrain;

inline int32_t Milli(float v) { return int32_t(std::lround(double(v) * 1000.0)); }
inline int32_t Milli(int v) { return v * 1000; }

// Current value of a parameter; false for an unknown ID.
inline bool ReadParam(const Settings& s, uint16_t id, int32_t& out) {
    switch(id) {
        case 1: out = Milli(s.tempo); return true;
        case 2: out = Milli(s.swing); return true;
        case 3: out = Milli(s.length); return true;
        case 4: out = Milli(s.transpose); return true;
        case 5: out = Milli(int(s.mode)); return true;
        case 6: out = Milli(int(s.scale)); return true;
        case 7: out = Milli(int(s.clock_source)); return true;
        case 8: out = Milli(int(s.divider)); return true;
        case 9: out = Milli(int(s.gate2_mode)); return true;
        case 10: out = Milli(int(s.cv1_mode)); return true;
        case 11: out = Milli(int(s.cv2_mode)); return true;
        case 12: out = Milli(int(s.seq_channel)); return true;
        case 13: out = Milli(int(s.voice_channel)); return true;
        case 14: out = Milli(int(s.usb_out)); return true;
        case 15: out = Milli(int(s.din_out)); return true;
        case 16: out = Milli(int(s.accent_velocity)); return true;
        case 17: out = Milli(int(s.velocity)); return true;
        case 18: out = Milli(s.glide_ms); return true;
        case 19: out = Milli(int(s.clock_out)); return true;
        default: return false;
    }
}

// Applies a constrained value. Read-only parameters (play_mode follows SW18) refuse.
inline bool WriteParam(Settings& s, uint16_t id, int32_t value) {
    const sq::ParamDesc* d = sq::FindParam(sq::kStepSeqComponent, id);
    if(!d || (d->flags & sq::FlagReadonly))
        return false;
    value = Constrain(*d, value);
    const float f = float(value) / 1000.f;
    const int i = int(value / 1000);
    switch(id) {
        case 1: s.tempo = f; break;
        case 2: s.swing = f; break;
        case 3: s.length = i; break;
        case 4: s.transpose = i; break;
        case 6: s.scale = Scale(i); break;
        case 7: s.clock_source = ClockSource(i); break;
        case 8: s.divider = Divider(i); break;
        case 9: s.gate2_mode = Gate2Mode(i); break;
        case 10: s.cv1_mode = Cv1Mode(i); break;
        case 11: s.cv2_mode = Cv2Mode(i); break;
        case 12: s.seq_channel = uint8_t(i); break;
        case 13: s.voice_channel = uint8_t(i); break;
        case 14: s.usb_out = i != 0; break;
        case 15: s.din_out = i != 0; break;
        case 16: s.accent_velocity = uint8_t(i); break;
        case 17: s.velocity = uint8_t(i); break;
        case 18: s.glide_ms = f; break;
        case 19: s.clock_out = i != 0; break;
        default: return false;
    }
    return true;
}

// Panel-writable value to pot index (VR5-VR8) for the pick-up after a panel write.
inline int PotOfParam(uint16_t id) { return id >= 1 && id <= 4 ? int(id) + 3 : -1; }

} // namespace step_seq
