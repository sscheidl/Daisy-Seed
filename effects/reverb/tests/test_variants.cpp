// SPDX-License-Identifier: MIT
// Six-mode transition regression carried forward from the 28.10 experiment.
#include "../src/ambient_reverb.hpp"
#include "../src/wet_path.hpp"
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace ambient;
static void Require(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static Parameters Base(Mode mode) {
    Parameters p = AmbientReverb::Preset(mode);
    p.mix = .7f;
    p.outputGain = .8f;
    p.texture = 1;
    p.modDepthMs = 3;
    return p;
}
// Deterministic noise bursts with silence in between.
struct Source {
    uint32_t seed = 12345;
    Stereo Next(unsigned n) {
        seed = seed * 1664525u + 1013904223u;
        const float noise = float(seed >> 8) * (2.f / 16777215.f) - 1;
        const float gate = (n / 12000) % 4 == 0 ? .3f : 0.f;
        return {gate * noise, gate * (.5f * noise + .2f * std::sin(.01f * float(n)))};
    }
};
static int Check() {
    auto memory = std::make_unique<Memory>();
    WetPath wet;
    Parameters p = Base(Mode::Cloud);
    wet.SetParameters(p);
    Require(wet.Prepare(48000, *memory), "prepare failed");
    // Mode changes through all shifter combinations, including back to none.
    constexpr Mode order[] = {Mode::Cloud, Mode::Abyss, Mode::Halo, Mode::Shimmer,
                              Mode::Abyss, Mode::Bloom, Mode::Shimmer, Mode::Scatter};
    Source source;
    float peak = 0;
    for (unsigned n = 0; n < 8 * 24000; ++n) {
        if (n % 24000 == 0) {
            p = Base(order[n / 24000]);
            p.stretch = float((n / 24000) % 3) * .5f;
            wet.SetParameters(p);
        }
        if (n % 32 == 0)
            wet.ServiceFilter();
        const Stereo y = wet.Process(source.Next(n), true);
        Require(std::isfinite(y.left) && std::isfinite(y.right), "mode change nonfinite");
        peak = std::max({peak, std::abs(y.left), std::abs(y.right)});
    }
    Require(peak < 1, "mode changes clip");
    Require(wet.GetDiagnostics().guardEvents == 0, "guard engaged during mode changes");
    std::printf("PASS variants check: mode changes bounded (peak %.3f)\n", double(peak));
    return 0;
}
int main() { return Check(); }
