// SPDX-License-Identifier: MIT
// Host tests of the Filter FX core: contract, every mode audible and stereo-independent,
// Drive stage in and out, LFO shapes and stereo offset, envelope follower both ways, wet
// ceiling, dry/wet, parameter table, full-modulation stability and the golden hash.
#include "filter_fx/filter_fx.h"
#include "filter_fx/host.h"
#include "filter_fx/params.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace seed::fx;
using namespace seed::fx::filter_fx;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr std::size_t kBlock = 48;

struct Rig {
    std::unique_ptr<FilterFx> fx = std::make_unique<FilterFx>();
    std::unique_ptr<FilterFx::Storage> storage = std::make_unique<FilterFx::Storage>();
    float in_l[kBlock] = {}, in_r[kBlock] = {}, out_l[kBlock] = {}, out_r[kBlock] = {};
    unsigned sample = 0;
    Rig() { CHECK(fx->Prepare({48000, kBlock}, *storage)); }
    // A sine in both channels; returns the output peak over `blocks`.
    double Run(unsigned blocks, double hz, double amplitude, double* rms_l = nullptr, double* rms_r = nullptr) {
        double peak = 0., sum_l = 0., sum_r = 0.;
        for(unsigned b = 0; b < blocks; ++b) {
            for(std::size_t i = 0; i < kBlock; ++i, ++sample)
                in_l[i] = in_r[i] = float(amplitude * std::sin(2. * kPi * hz / 48000. * double(sample)));
            CHECK(fx->Process({in_l, in_r, out_l, out_r, kBlock}));
            for(std::size_t i = 0; i < kBlock; ++i) {
                CHECK(std::isfinite(out_l[i]) && std::isfinite(out_r[i]));
                peak = std::fmax(peak, std::fmax(std::fabs(out_l[i]), std::fabs(out_r[i])));
                sum_l += double(out_l[i]) * out_l[i];
                sum_r += double(out_r[i]) * out_r[i];
            }
        }
        if(rms_l)
            *rms_l = std::sqrt(sum_l / (double(blocks) * kBlock));
        if(rms_r)
            *rms_r = std::sqrt(sum_r / (double(blocks) * kBlock));
        return peak;
    }
};

static void TestContract() {
    auto fx = std::make_unique<FilterFx>();
    auto storage = std::make_unique<FilterFx::Storage>();
    float a[48] = {}, b[48] = {}, c[48] = {}, d[48] = {};
    c[0] = 0.7f;
    CHECK(!fx->Process({a, b, c, d, 48}) && c[0] == 0.7f); // not prepared
    CHECK(!fx->Prepare({44100, 48}, *storage));
    CHECK(!fx->Prepare({48000, 0}, *storage));
    CHECK(!fx->Prepare({48000, 96}, *storage));
    CHECK(fx->Prepare({48000, 48}, *storage));
    CHECK(!fx->Process({a, b, c, d, 49}) && c[0] == 0.7f);
    CHECK(!fx->Process({nullptr, b, c, d, 48}));
    CHECK(!fx->Process({a, b, c, c, 48}));
    CHECK(fx->Process({nullptr, nullptr, nullptr, nullptr, 0}));
    // In place per channel, as the contract allows.
    Rig one, two;
    for(unsigned n = 0; n < 200; ++n) {
        float l[48], r[48];
        for(unsigned i = 0; i < 48; ++i) {
            l[i] = 0.3f * float(std::sin(0.05 * double(n * 48 + i)));
            r[i] = 0.2f * float(std::cos(0.03 * double(n * 48 + i)));
        }
        CHECK(two.fx->Process({l, r, two.out_l, two.out_r, 48}));
        CHECK(one.fx->Process({l, r, l, r, 48}));
        CHECK(!std::memcmp(l, two.out_l, sizeof(l)) && !std::memcmp(r, two.out_r, sizeof(r)));
    }
}

static void TestModes() {
    // Cutoff low: LP stops a 5 kHz tone and HP passes it; a 50 Hz tone the other way round.
    const auto level = [](Mode mode, double hz, float cutoff) {
        Rig rig;
        Parameters p;
        p.mode = mode;
        p.cutoff = cutoff;
        p.resonance = 0.f;
        rig.fx->SetParameters(p);
        rig.Run(400, hz, 0.25);
        double rms = 0.;
        rig.Run(400, hz, 0.25, &rms);
        return 20. * std::log10(rms / (0.25 / std::sqrt(2.)) + 1e-12);
    };
    for(Mode mode : {Mode::Lp6, Mode::Lp12, Mode::Lp18, Mode::Lp24}) {
        CHECK(std::fabs(level(mode, 50., 100.f)) < 1.); // cutoff 2.6 kHz: passes
        CHECK(level(mode, 15000., 60.f) < -20.);        // cutoff 262 Hz: gone
    }
    CHECK(level(Mode::Hp, 50., 84.f) < -60.);
    CHECK(std::fabs(level(Mode::Hp, 15000., 84.f)) < 1.);
    CHECK(level(Mode::Bp, 50., 84.f) < -40. && level(Mode::Bp, 15000., 84.f) < -15.);
    CHECK(std::fabs(level(Mode::Bp, 1046.5, 84.f)) < 0.5);
    CHECK(level(Mode::Notch, 1046.5, 84.f) < -40. && std::fabs(level(Mode::Notch, 50., 84.f)) < 0.5);
    CHECK(level(Mode::Phaser, 433.5, 84.f) < -30.);
}

static void TestDrive() {
    // Drive 0 %: the filter alone. Drive up: more harmonics at the same level (auto level).
    const auto third = [](float drive) {
        Rig rig;
        Parameters p;
        p.cutoff = 135.f;
        p.resonance = 0.f;
        p.drive = drive;
        rig.fx->SetParameters(p);
        rig.Run(1500, 500., 0.3); // past Drive's 300 ms level windows
        double c = 0., s = 0., c1 = 0., s1 = 0.;
        const unsigned count = 96 * 50;
        unsigned index = 0;
        for(unsigned b = 0; b < 100; ++b) {
            rig.Run(1, 500., 0.3);
            for(std::size_t i = 0; i < kBlock; ++i, ++index) {
                const double t = 2. * kPi * 500. / 48000. * double(index);
                c += rig.out_l[i] * std::cos(3. * t);
                s += rig.out_l[i] * std::sin(3. * t);
                c1 += rig.out_l[i] * std::cos(t);
                s1 += rig.out_l[i] * std::sin(t);
            }
        }
        const double fundamental = 2. * std::hypot(c1, s1) / count, harmonic = 2. * std::hypot(c, s) / count;
        std::printf("drive %3.0f %%: fundamental %.3f, third harmonic %.1f dB\n", double(drive), fundamental,
                    20. * std::log10(harmonic / fundamental + 1e-12));
        return harmonic / fundamental;
    };
    CHECK(third(0.f) < 1e-3);
    CHECK(third(80.f) > 0.02);
    // Moving the drive down to 0 must reach the plain filter again, without a step.
    Rig rig;
    Parameters p;
    p.drive = 60.f;
    rig.fx->SetParameters(p);
    rig.Run(100, 500., 0.3);
    p.drive = 0.f;
    rig.fx->SetParameters(p);
    double previous = 0., largest = 0.;
    for(unsigned b = 0; b < 40; ++b) {
        rig.Run(1, 500., 0.3);
        for(std::size_t i = 0; i < kBlock; ++i) {
            largest = std::fmax(largest, std::fabs(double(rig.out_l[i]) - previous));
            previous = rig.out_l[i];
        }
    }
    CHECK(largest < 0.15);
}

static void TestModulation() {
    // LFO: the cutoff swings by the depth around the centre, for every shape.
    for(LfoShape shape : {LfoShape::Sine, LfoShape::Triangle, LfoShape::Saw, LfoShape::SampleHold}) {
        Rig rig;
        Parameters p;
        p.cutoff = 80.f;
        p.lfo_rate = 10.f;
        p.lfo_depth = 24.f;
        p.lfo_shape = shape;
        rig.fx->SetParameters(p);
        double lo = 1e9, hi = -1e9, offset = 0.;
        for(unsigned b = 0; b < 4000; ++b) {
            rig.Run(1, 200., 0.);
            lo = std::fmin(lo, rig.fx->Note(0));
            hi = std::fmax(hi, rig.fx->Note(0));
            offset = std::fmax(offset, std::fabs(rig.fx->Note(0) - rig.fx->Note(1)));
        }
        CHECK(lo >= 80. - 24.5 && hi <= 80. + 24.5);
        CHECK(hi - lo > (shape == LfoShape::SampleHold ? 24. : 45.));
        CHECK(offset < 1e-4); // stereo off: both channels the same
    }
    // Stereo: the channels move apart; a quarter cycle apart at the sine.
    for(LfoShape shape : {LfoShape::Sine, LfoShape::SampleHold}) {
        Rig rig;
        Parameters p;
        p.cutoff = 80.f;
        p.lfo_rate = 5.f;
        p.lfo_depth = 12.f;
        p.lfo_stereo = true;
        p.lfo_shape = shape;
        rig.fx->SetParameters(p);
        double apart = 0.;
        for(unsigned b = 0; b < 2000; ++b) {
            rig.Run(1, 200., 0.);
            apart = std::fmax(apart, std::fabs(rig.fx->Note(0) - rig.fx->Note(1)));
        }
        CHECK(apart > 5.);
    }
    // Envelope: opens with the level in the chosen direction and falls back with the release.
    for(float amount : {36.f, -36.f}) {
        Rig rig;
        Parameters p;
        p.cutoff = 70.f;
        p.env_amount = amount;
        p.env_release = 50.f;
        rig.fx->SetParameters(p);
        rig.Run(100, 300., 0.);
        CHECK(std::fabs(rig.fx->Note(0) - 70.f) < 0.5f);
        rig.Run(100, 300., 0.5);
        const float open = rig.fx->Note(0) - 70.f;
        std::printf("envelope at -6 dBFS: %.1f of %.0f semitones\n", double(open), double(amount));
        CHECK(open * amount > 0.f && std::fabs(open) > 0.7f * std::fabs(amount));
        rig.Run(1000, 300., 0.);
        CHECK(std::fabs(rig.fx->Note(0) - 70.f) < 0.5f);
    }
}

static void TestCeilingAndMix() {
    // Unchanged below the knee, monotonic, and never above 1.
    CHECK(WetCeiling(0.5f) == 0.5f && WetCeiling(-0.75f) == -0.75f);
    float previous = 0.f;
    for(float x = 0.f; x < 8.f; x += 0.001f) {
        const float y = WetCeiling(x);
        CHECK(y >= previous && y <= 1.f && WetCeiling(-x) == -y);
        previous = y;
    }
    CHECK(WetCeiling(100.f) == 1.f);
    // A loud tone on the resonance stays inside the ceiling.
    Rig rig;
    Parameters p;
    p.cutoff = 69.f;
    p.resonance = 100.f;
    rig.fx->SetParameters(p);
    CHECK(rig.Run(2000, 440., 1.) <= 1.);
    // Dry/wet: exact ends, no step.
    FilterDryWet mix;
    mix.Reset(0.f);
    float dry[48], wet[48], wet2[48];
    for(unsigned i = 0; i < 48; ++i) {
        dry[i] = 0.25f;
        wet[i] = wet2[i] = 0.75f;
    }
    mix.Process(dry, dry, wet, wet2, 48);
    CHECK(wet[47] == 0.25f);
    mix.Set(1.f);
    float last = 0.25f;
    for(unsigned b = 0; b < 12; ++b) {
        for(unsigned i = 0; i < 48; ++i)
            wet[i] = wet2[i] = 0.75f;
        mix.Process(dry, dry, wet, wet2, 48);
        for(float v : wet) {
            CHECK(v - last < 0.002f);
            last = v;
        }
    }
    CHECK(last == 0.75f);
}

static void TestParameters() {
    Parameters p;
    for(uint8_t i = 0; i < sq::kFilterFxComponent.count; ++i) {
        const sq::ParamDesc& d = sq::kFilterFxComponent.params[i];
        int32_t value = 0;
        CHECK(ReadParam(p, d.id, value)); // the struct's defaults are the contract's
        CHECK(std::abs(value - d.def) <= 1);
        for(int32_t v : {d.min, d.max, d.def}) {
            CHECK(WriteParam(p, d.id, v));
            CHECK(ReadParam(p, d.id, value) && std::abs(value - v) <= 1);
        }
        CHECK(WriteParam(p, d.id, d.def));
    }
    int32_t value = 0;
    CHECK(!ReadParam(p, 99, value) && !WriteParam(p, 99, 0));
    // Out-of-range structs reach the DSP sanitized.
    Rig rig;
    Parameters bad;
    bad.cutoff = std::nanf("");
    bad.resonance = 1e9f;
    bad.mode = Mode(77);
    bad.lfo_rate = -1.f;
    bad.lfo_shape = LfoShape(9);
    bad.bass_comp = BassComp(9);
    rig.fx->SetParameters(bad);
    CHECK(rig.fx->Requested().mode == Mode::Lp24 && rig.fx->Requested().resonance == 100.f);
    rig.Run(100, 440., 0.5);
}

static void TestStability() {
    // Everything at the top at once, noise in, all modes; non-finite input must not stick.
    uint32_t seed = 99u;
    const auto noise = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(int32_t(seed)) / 2147483648.f;
    };
    for(unsigned m = 0; m < seed::dsp::filter::kModeCount; ++m) {
        Rig rig;
        Parameters p;
        p.mode = Mode(m);
        p.resonance = 100.f;
        p.drive = 100.f;
        p.lfo_rate = 20.f;
        p.lfo_depth = 48.f;
        p.lfo_shape = LfoShape::SampleHold;
        p.lfo_stereo = true;
        p.env_amount = 48.f;
        p.env_release = 10.f;
        rig.fx->SetParameters(p);
        for(unsigned b = 0; b < 8000; ++b) {
            for(std::size_t i = 0; i < kBlock; ++i) {
                rig.in_l[i] = noise();
                rig.in_r[i] = noise();
            }
            if(b % 1000 == 500) {
                rig.in_l[3] = std::nanf("");
                rig.in_r[9] = INFINITY;
            }
            CHECK(rig.fx->Process({rig.in_l, rig.in_r, rig.out_l, rig.out_r, kBlock}));
            for(std::size_t i = 0; i < kBlock; ++i)
                CHECK(std::isfinite(rig.out_l[i]) && std::isfinite(rig.out_r[i]) && std::fabs(rig.out_l[i]) <= 1.f &&
                      std::fabs(rig.out_r[i]) <= 1.f);
        }
    }
}

static void TestGolden() {
    uint32_t hash = 2166136261u;
    for(unsigned m = 0; m < seed::dsp::filter::kModeCount; ++m) {
        Rig rig;
        Parameters p;
        p.mode = Mode(m);
        p.cutoff = 84.f;
        p.resonance = 70.f;
        p.drive = 40.f;
        p.lfo_depth = 12.f;
        p.lfo_rate = 3.f;
        p.env_amount = 12.f;
        rig.fx->SetParameters(p);
        for(unsigned b = 0; b < 1000; ++b) {
            for(std::size_t i = 0; i < kBlock; ++i) {
                rig.in_l[i] = float((b * 48 + unsigned(i)) % 109) / 109.f - 0.5f;
                rig.in_r[i] = float((b * 48 + unsigned(i)) % 151) / 151.f - 0.5f;
            }
            CHECK(rig.fx->Process({rig.in_l, rig.in_r, rig.out_l, rig.out_r, kBlock}));
            for(std::size_t i = 0; i < kBlock; ++i)
                for(float v : {rig.out_l[i], rig.out_r[i]}) {
                    uint32_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    for(int k = 0; k < 4; ++k) {
                        hash ^= (bits >> (8 * k)) & 0xFFu;
                        hash *= 16777619u;
                    }
                }
        }
    }
    std::printf("golden hash 0x%08X\n", unsigned(hash));
    CHECK(hash == 0xBDFBAE6Au);
}

int main() {
    TestContract();
    TestModes();
    TestDrive();
    TestModulation();
    TestCeilingAndMix();
    TestParameters();
    TestStability();
    TestGolden();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: Filter FX core\n");
    return 0;
}
