// SPDX-License-Identifier: MIT
// Diagnostic CPU switches of the wet path. "write"/"compare" render every mode
// from Prepare without a mode change (Stretch endpoints, sweeps, noise bursts):
// AMBIENT_PITCH_GATE must match the default build bit for bit there. "check"
// runs the build's own invariants: bounded mode changes for the gate, and the
// rate range for AMBIENT_WET_MAX_HZ.
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
static float Stretch(unsigned n, unsigned total) {
    const float t = float(n) / float(total);
    return t < .25f ? 0.f : t < .5f ? (t - .25f) * 4 : t < .75f ? 1.f : 4 * (1 - t);
}
static int Render(bool write, const char *path) {
    std::FILE *file = std::fopen(path, write ? "wb" : "rb");
    Require(file != nullptr, "cannot open reference file");
    auto memory = std::make_unique<Memory>();
    uint64_t samples = 0, different = 0;
    constexpr unsigned kFrames = 3 * 48000;
    for (unsigned m = 0; m <= unsigned(Mode::Shimmer); ++m) {
        WetPath wet;
        Parameters p = Base(Mode(m));
        wet.SetParameters(p); // Mode before Prepare: no crossfade from Cloud.
        Require(wet.Prepare(48000, *memory), "prepare failed");
        Source source;
        for (unsigned n = 0; n < kFrames; ++n) {
            if (n % 480 == 0) {
                p.stretch = Stretch(n, kFrames);
                wet.SetParameters(p);
            }
            if (n % 32 == 0)
                wet.ServiceFilter();
            const Stereo y = wet.Process(source.Next(n), true);
            const float actual[2] = {y.left, y.right};
            Require(std::isfinite(actual[0]) && std::isfinite(actual[1]), "nonfinite output");
            if (write) {
                Require(std::fwrite(actual, sizeof(actual), 1, file) == 1, "write failed");
            } else {
                float reference[2];
                Require(std::fread(reference, sizeof(reference), 1, file) == 1, "reference short");
                different += std::memcmp(&actual[0], &reference[0], 4) != 0;
                different += std::memcmp(&actual[1], &reference[1], 4) != 0;
            }
            samples += 2;
        }
    }
    if (!write)
        Require(std::fgetc(file) == EOF, "reference longer than render");
    std::fclose(file);
    std::printf("%s variants %s: %" PRIu64 " samples, %" PRIu64 " different\n",
                different == 0 ? "PASS" : "FAIL", write ? "reference" : "compare", samples,
                different);
    return different == 0 ? 0 : 1;
}
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
#if AMBIENT_WET_MAX_HZ
    // Wet engine rate at both Stretch endpoints: core calls per host second.
    for (const float stretch : {0.f, 1.f}) {
        WetPath capped;
        Parameters q = Base(Mode::Cloud);
        q.stretch = stretch;
        capped.SetParameters(q);
        Require(capped.Prepare(48000, *memory), "capped prepare failed");
        for (unsigned n = 0; n < 48000; ++n)
            capped.Process({0, 0}, true);
        const double calls = double(capped.GetDiagnostics().samples);
        const double expected = stretch == 0 ? double(AMBIENT_WET_MAX_HZ) : 12000.0;
        Require(std::abs(calls - expected) <= 2, "wet engine rate off at a Stretch endpoint");
        std::printf("Stretch %.0f: %.0f wet samples per host second\n", double(stretch), calls);
    }
#endif
    std::printf("PASS variants check: mode changes bounded (peak %.3f)\n", double(peak));
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "check") == 0)
        return Check();
    Require(argc == 3, "usage: test_variants write|compare FILE | check");
    return Render(std::strcmp(argv[1], "write") == 0, argv[2]);
}
