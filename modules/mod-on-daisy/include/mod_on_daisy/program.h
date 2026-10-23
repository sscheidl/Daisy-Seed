#pragma once
// Compiled, immutable form of a patch for the audio IRQ (ARCHITECTURE.md § 3.4).
// Compile() runs in the main loop only: it calls libm (rate -> increment).
#include "patch.h"
#include "sources.h"

namespace mod {
struct Connection {
    uint8_t source = 0, target = 0;
    float gain = 0.0f; // depth/100 * span
};
struct SourceConfig {
    uint32_t inc = 0;  // free running: phase increment per sample
    Ratio ratio{1, 24};
    uint8_t shape = Sine; // LFO shape or random mode
    uint8_t sync = Free;
    uint8_t gate = 0;
    uint8_t retrigger = 0;
};
struct Program {
    uint8_t count = 0;            // active global connections
    bool active = false;          // false: offsets are exactly +0.0f
    Connection conn[kMaxActive]{};
    float voice_gain[kTargets]{}; // Poly AT on per_voice targets
    SourceConfig lfo[2]{}, random{};
    float cv_offset[2]{};         // device calibration (not part of the patch)
};

inline SourceConfig CompileSource(uint16_t rate, uint8_t shape, uint8_t sync, uint8_t gate, uint8_t division,
                                  uint8_t retrigger, float sample_rate) {
    SourceConfig c;
    c.inc = PhaseIncrement(RateToHz(rate), sample_rate);
    c.shape = shape;
    c.sync = sync;
    c.gate = gate;
    c.retrigger = retrigger;
    if(sync == GateClock)
        c.ratio = kGateDivisions[division < kGateDivisionCount ? division : kDefaultGateDivision];
    else
        c.ratio = kMidiDivisions[division < kMidiDivisionCount ? division : kDefaultMidiDivision];
    return c;
}

// Expects a patch accepted by Validate().
inline Program Compile(const Patch& patch, const InstrumentInfo& info, float sample_rate,
                       const float (&cv_offset)[2]) {
    Program p;
    for(std::size_t s = 0; s < kSources; ++s)
        for(std::size_t t = 0; t < kTargets; ++t) {
            if(!patch.Active(s, t))
                continue;
            const float gain = float(patch.depth[s][t]) / float(kMaxDepth) * info.targets[t].span;
            if(s == PolyAt && info.targets[t].per_voice)
                p.voice_gain[t] = gain;
            else if(p.count < kMaxActive)
                p.conn[p.count++] = {uint8_t(s), uint8_t(t), gain};
            p.active = true;
        }
    for(std::size_t i = 0; i < 2; ++i) {
        const LfoParams& l = patch.lfo[i];
        p.lfo[i] = CompileSource(l.rate, l.shape, l.sync, l.gate, l.division, l.retrigger, sample_rate);
    }
    const RandomParams& r = patch.random;
    p.random = CompileSource(r.rate, r.mode, r.sync, r.gate, r.division, 0, sample_rate);
    p.cv_offset[0] = cv_offset[0];
    p.cv_offset[1] = cv_offset[1];
    return p;
}
}
