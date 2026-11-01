// SPDX-License-Identifier: MIT
// Exercise the shared FX policy without an instrument or board.
#include "../src/wet_path.hpp"
#include "../src/fx_adapter.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace ambient;

static void ClockBounds() {
    auto memory = std::make_unique<Memory>();
    for (float fs : {32000.f, 32768.f, 44100.f, 48000.f, 96000.f}) {
        for (float stretch : {0.f, .5f, 1.f}) {
            WetPath path;
            Parameters p;
            p.stretch = stretch;
            p.mix = 0;
            p.outputGain = 1;
            path.SetParameters(p);
            assert(path.Prepare(fs, *memory));
            const float expected = 32000.f * std::pow(12000.f / 32000.f, stretch);
            assert(std::abs(path.InternalSampleRate() - expected) < .01f);
            for (unsigned n = 0; n < unsigned(fs); ++n) {
                const Stereo input{.1f, -.03f};
                const auto y = path.Process(input, true);
                assert(y.left == input.left && y.right == input.right);
            }
            // Count actual core work, not just the requested scheduler ratio.
            assert(std::abs(double(path.GetDiagnostics().samples) - expected) < 2);
            p.stretch = 1 - stretch;
            path.SetParameters(p);
            for (unsigned n = 0; n < unsigned(fs); ++n) {
                if (n % 100 == 0) path.ServiceFilter();
                const auto y = path.Process({}, true);
                assert(std::isfinite(y.left) && std::isfinite(y.right));
                assert(path.InternalSampleRate() >= 11999.f &&
                       path.InternalSampleRate() <= 32001.f);
            }
        }
    }
    std::puts("PASS: actual 32–12 kHz core work at five codec rates; dry path unchanged");
}

static void PitchWork() {
    auto memory = std::make_unique<Memory>();
    AmbientReverb fx;
    Parameters p;
    p.mix = p.outputGain = 1;
    assert(fx.Prepare(48000, *memory));
    constexpr unsigned count = 4800, crossfade = 1920;
    for (Mode mode : {Mode::Cloud, Mode::Halo, Mode::Shimmer, Mode::Abyss,
                      Mode::Scatter, Mode::Bloom, Mode::Halo, Mode::Cloud}) {
        p.mode = mode;
        fx.SetParameters(p);
        for (unsigned n = 0; n < crossfade; ++n) fx.Process({});
        const auto before = fx.GetDiagnostics();
        for (unsigned n = 0; n < count; ++n) {
            // Repeated control-rate updates must not restart an active shifter.
            if (n % 960 == 0) fx.SetParameters(p);
            const auto y = fx.Process({.04f * std::sin(.03f * n), -.02f});
            assert(std::isfinite(y.left) && std::isfinite(y.right));
        }
        const auto after = fx.GetDiagnostics();
        const bool up = mode == Mode::Halo || mode == Mode::Shimmer;
        const bool down = mode == Mode::Abyss;
        assert(after.upSamples - before.upSamples == (up ? count : 0));
        assert(after.downSamples - before.downSamples == (down ? count : 0));
        assert(after.guardEvents == 0);
    }
    // During a Halo -> Abyss crossfade BOTH contributions must keep running.
    p.mode = Mode::Halo;
    fx.SetParameters(p, true);
    for (unsigned n = 0; n < count; ++n) fx.Process({});
    const auto before = fx.GetDiagnostics();
    p.mode = Mode::Abyss;
    fx.SetParameters(p);
    for (unsigned n = 0; n < crossfade - 1; ++n) fx.Process({});
    const auto during = fx.GetDiagnostics();
    assert(during.upSamples - before.upSamples == crossfade - 1);
    assert(during.downSamples - before.downSamples == crossfade - 1);
    std::puts("PASS: mode-specific pitch work, repeated controls and outgoing crossfade");
}

static void AdapterUsesSharedClock() {
    auto memory = std::make_unique<seed::fx::AmbientReverb::Storage>();
    seed::fx::AmbientReverb fx;
    Parameters p;
    p.mode = Mode::Shimmer;
    fx.SetParameters(p);
    assert(fx.Prepare({48000, 48}, *memory));
    float l[48]{}, r[48]{};
    for (unsigned n = 0; n < 1000; ++n)
        assert(fx.Process({l, r, l, r, 48}));
    assert(std::abs(fx.InternalSampleRate() - 32000.f) < .01f);
    assert(std::abs(double(fx.GetDiagnostics().samples) - 32000) < 2);
    assert(fx.GetDiagnostics().upSamples == fx.GetDiagnostics().samples);
    assert(fx.GetDiagnostics().downSamples == 0);
    p.stretch = 1;
    fx.SetParameters(p);
    for (unsigned n = 0; n < 1000; ++n)
        assert(fx.Process({l, r, l, r, 48}));
    assert(std::abs(fx.InternalSampleRate() - 12000.f) < 1);
    std::puts("PASS: wet-only adapter shares clock cap and pitch policy");
}

static void RestartHasNoOldAudio() {
    auto memory = std::make_unique<Memory>();
    for (float ratio : {.5f, 2.f}) {
        Octave pitch;
        pitch.Prepare(48000, memory->pitch[0], memory->pitch[1], ratio);
        double energy = 0;
        for (unsigned n = 0; n < 24000; ++n) {
            const auto y = pitch.Process({.1f * std::sin(.05f * n), .04f});
            energy += y.left * y.left + y.right * y.right;
        }
        assert(energy > .01);
        // Restart neither clears external RAM nor leaks it into a new program.
        const auto old = memory->pitch;
        pitch.Restart();
        assert(old == memory->pitch);
        for (unsigned n = 0; n < 24000; ++n) {
            const auto y = pitch.Process({});
            assert(y.left == 0 && y.right == 0);
        }
        pitch.Restart();
        energy = 0;
        for (unsigned n = 0; n < 24000; ++n) {
            const auto y = pitch.Process({.1f * std::sin(.05f * n), .04f});
            energy += y.left * y.left + y.right * y.right;
        }
        assert(energy > .01);
    }
    std::puts("PASS: constant-time pitch restart, no stale audio, sound resumes");
}

int main() {
    ClockBounds();
    PitchWork();
    AdapterUsesSharedClock();
    RestartHasNoOldAudio();
}
