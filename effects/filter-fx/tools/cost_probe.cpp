// SPDX-License-Identifier: MIT
// The per-sample stages of Filter FX as plain functions, compiled by
// modules/filter-core/tools/arm_cost.py for Cortex-M7.
#include "filter_fx/filter_fx.h"
#include "filter_fx/host.h"

using seed::audio::StereoBlock;

extern "C" [[gnu::flatten]] bool cost_drive(seed::fx::Drive& f, const StereoBlock& b) { return f.Process(b); }
extern "C" [[gnu::flatten]] void cost_ladder_ramped(seed::dsp::filter::Ladder& f, const float* in, float* out, unsigned n) {
    f.Process(in, out, n);
}
extern "C" [[gnu::flatten]] void cost_ceiling(float* l, float* r, unsigned n) {
    for(unsigned i = 0; i < n; ++i) {
        l[i] = seed::fx::filter_fx::WetCeiling(l[i]);
        r[i] = seed::fx::filter_fx::WetCeiling(r[i]);
    }
}
extern "C" [[gnu::flatten]] void cost_dry_wet(seed::fx::FilterDryWet& m, const float* a, const float* b, float* c, float* d, unsigned n) {
    m.Process(a, b, c, d, n);
}
extern "C" [[gnu::flatten]] void cost_fade(seed::fx::DriveMixer& m, const float* a, const float* b, float* c, float* d, float* e,
                          float* f, unsigned n) {
    m.Process(a, b, c, d, e, f, n);
}
