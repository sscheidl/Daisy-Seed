// SPDX-License-Identifier: MIT
// The core's sample steps as plain functions without a loop, compiled by tools/arm_cost.py for
// Cortex-M7, so that the function length is the cost of one sample. Plus the per-call work.
#define FILTER_CORE_COST_PROBE 1
#include "filter_core/ladder.h"

using namespace seed::dsp::filter;

extern "C" [[gnu::flatten]] float cost_sample_clean(Ladder& f, float x, const float* c) { return f.CleanForCost(x, c); }
extern "C" [[gnu::flatten]] float cost_sample_age(Ladder& f, float x, const float* c) {
    return f.GeneralForCost<Model::Clean>(x, c);
}
extern "C" [[gnu::flatten]] float cost_sample_sum(Ladder& f, float x, const float* c) {
    return f.GeneralForCost<Model::Sum>(x, c);
}
extern "C" [[gnu::flatten]] float cost_sample_stages(Ladder& f, float x, const float* c) {
    return f.GeneralForCost<Model::Stages>(x, c);
}
extern "C" [[gnu::flatten]] void cost_call(Ladder& f, unsigned n) { f.AdvanceForCost(n); }
extern "C" [[gnu::flatten]] void cost_call_setup(Ladder& f) { f.SetupForCost(); }
