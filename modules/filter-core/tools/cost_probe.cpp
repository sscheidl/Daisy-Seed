// SPDX-License-Identifier: MIT
// The core's two paths as plain functions, compiled by tools/arm_cost.py for Cortex-M7.
#include "filter_core/ladder.h"

using seed::dsp::filter::Ladder;

extern "C" [[gnu::flatten]] void cost_ladder_ramped(Ladder& f, const float* in, float* out, unsigned n) { f.Process(in, out, n); }
extern "C" [[gnu::flatten]] void cost_ladder_fm(Ladder& f, const float* in, float* out, unsigned n, const float* fm) {
    f.Process(in, out, n, fm);
}
extern "C" [[gnu::flatten]] void cost_ladder_held(Ladder& f, const float* in, float* out, unsigned n) { f.ProcessHeld(in, out, n); }
