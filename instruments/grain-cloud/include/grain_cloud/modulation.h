#pragma once
// SPDX-License-Identifier: MIT
// R8: one matrix, one writer. The matrix is the existing Mod on Daisy core (one Patch, one
// compiled Program); this file adds no second matrix. The engine calls Tick() from its
// control tick, and that call is the only place modulated values are written. CellEdit is
// the simplified "one source, one target" view: it edits the Patch and nothing else.
#include "laws.h"
#include "mod_on_daisy/engine.h"
#include "params.h"
#include <cstddef>

namespace grain_cloud {

struct Modulated {
    float value[kModTargets]{};
    float operator[](Param param) const { return value[std::size_t(param)]; }
};

// LFOs, the random source and CV swing around zero; wheel and pressure rest at zero.
inline bool BipolarSource(uint8_t source) { return source <= mod::Cv2; }

// B5: a bipolar source moves upward from the base over its whole cycle, so the base stays
// the minimum and base 0 has no clamped half cycle. Negative depth inverts the phase and
// still moves upward. Reads the same Program as Engine::Apply.
inline float OneSided(const mod::Engine& engine, const mod::Program& program, std::size_t target, float base) {
    if(!program.active)
        return base;
    float sum = 0.f;
    for(std::size_t c = 0; c < program.count; ++c) {
        const mod::Connection& connection = program.conn[c];
        if(connection.target != target)
            continue;
        const float source = engine.Source(connection.source);
        if(!BipolarSource(connection.source))
            sum += connection.gain * source;
        else if(connection.gain >= 0.f)
            sum += connection.gain * (0.5f + 0.5f * source);
        else
            sum -= connection.gain * (0.5f - 0.5f * source);
    }
    return Clamp01(base + sum);
}

class Modulation {
  public:
    // Before the audio starts. Without a matrix the modulated values equal the base values.
    void Attach(mod::Engine* engine) { engine_ = engine; }

    void Tick(const float* base, std::size_t tick_frames, Modulated& out) {
        if(!engine_) {
            for(std::size_t t = 0; t < kModTargets; ++t)
                out.value[t] = base[t];
            return;
        }
        engine_->Tick(tick_frames);
        const mod::Program& program = engine_->Programs().Front();
        for(std::size_t t = 0; t < kModTargets; ++t)
            out.value[t] = kParamInfo[t].one_sided ? OneSided(*engine_, program, t, base[t])
                                                   : engine_->Apply(t, base[t]);
    }

  private:
    mod::Engine* engine_ = nullptr;
};

// The grid columns of this instrument, for the Mod on Daisy controller and panels.
inline mod::InstrumentInfo ModInfo(const char* version) {
    mod::InstrumentInfo info;
    info.key = "grain-cloud";
    info.version = version;
    info.targets[0] = {"Position", "+-100%", 1.0f, false, false};
    info.targets[1] = {"Spray", "+100%", 1.0f, false, false};
    info.targets[2] = {"Size", "+10 oct", 1.0f, false, false};
    info.targets[3] = {"Density", "+11 oct", 1.0f, false, false};
    info.targets[4] = {"Pitch", "+-12 st", 0.25f, false, false};
    info.targets[5] = {"Window", "+-100%", 1.0f, false, false};
    info.targets[6] = {"Direction", "+-100%", 1.0f, false, false};
    info.targets[7] = {"Level", "+-100%", 1.0f, false, false};
    return info;
}

// Main loop: the simplified view. Changes one cell of the Patch, never a target parameter.
// The caller validates, compiles and publishes the Patch as for any other edit.
inline void CellEdit(mod::Patch& patch, std::size_t source, std::size_t target, int8_t depth) {
    patch.depth[source][target] = depth;
    patch.SetEnabled(source, target, depth != 0);
}

} // namespace grain_cloud
