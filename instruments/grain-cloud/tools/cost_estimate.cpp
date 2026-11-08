// SPDX-License-Identifier: MIT
// Paper cost of the Grain Cloud core: operation counts from its constants, multiplied by
// ASSUMED cycle figures. Nothing here ran on a Cortex-M7. Every cycle and percent figure
// is NOT MEASURED. Stage 3 replaces them with measurements on the kit.
#include "grain_cloud/engine.h"
#include <cstdio>

using namespace grain_cloud;

namespace {
// Assumptions, cycles at 480 MHz. Not taken from any measurement.
constexpr double kCyclesPerGrainSample = 40.0; // two PCM reads, two lerps, window, two MACs, steps
constexpr double kCyclesPerBusSample = 8.0;    // ramp and two MACs, per active voice
constexpr double kCyclesPerVoiceTick = 250.0;  // pitch and count law, envelope
constexpr double kCyclesMatrixTick = 600.0;    // Mod on Daisy tick with eight connections
constexpr double kCyclesPerWindowPoint = 5.0;
constexpr double kCyclesPerBirth = 300.0;
constexpr double kBudget = 480000.0;           // one 1 ms block at 480 MHz
constexpr double kTarget = 0.6;                // spec: measured maxima below 60 %

double Percent(double cycles) { return cycles / kBudget * 100.0; }
}

int main() {
    const double frames = 48.0, voices = 8.0;
    std::printf("Grain Cloud paper cost. All cycle and percent figures: NOT MEASURED (assumptions).\n");
    std::printf("State: Engine %zu bytes, Grain %zu bytes, window table %zu bytes, shapes %zu bytes\n\n",
                sizeof(Engine), sizeof(Grain), sizeof(float) * kWindowPoints,
                sizeof(float) * kWindowPoints * kWindowShapes);
    const double tick = kCyclesMatrixTick + voices * kCyclesPerVoiceTick;
    const double morph = double(kWindowPoints) * kCyclesPerWindowPoint;
    const double bus = voices * frames * kCyclesPerBusSample;
    const double births = double(kMaxBirthsPerBlock) * kCyclesPerBirth;
    std::printf("Per block, 8 voices: tick %.0f, morph %.0f (only while morphing), buses %.0f, birth bound %.0f\n\n",
                tick, morph, bus, births);
    std::printf("%8s %14s %10s %20s %10s\n", "grains", "grain samples", "render %", "worst block cycles", "worst %");
    for(int grains : {0, 16, 32, 64, 128, 256}) {
        const double samples = double(grains) * frames;
        const double render = samples * kCyclesPerGrainSample;
        const double worst = render + bus + tick + morph + births;
        std::printf("%8d %14.0f %10.1f %20.0f %10.1f%s\n", grains, samples, Percent(render), worst, Percent(worst),
                    worst > kBudget * kTarget ? "  over the 60 % target" : "");
    }
    std::printf("\nStart cloud: %u births per block at most, so 256 grains take %u blocks.\n", kMaxBirthsPerBlock,
                256u / kMaxBirthsPerBlock);
    std::printf("Control work per second at 1 kHz is 4x that at 250 Hz; see docs/BEHAVIOUR.md.\n");
    return 0;
}
