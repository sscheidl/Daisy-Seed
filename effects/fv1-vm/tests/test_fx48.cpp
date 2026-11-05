// SPDX-License-Identifier: MIT
// Host tests for the 48 kHz adapter (include/fv1vm/fv1_fx48.h) with the own demo programs.
#include "fv1vm/fv1_fx48.h"
#include "fv1_firmware.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace seed::fx::fv1;

static int failures = 0;
#define CHECK(cond, msg)                                                                                 \
    do {                                                                                               \
        if(!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++failures; }          \
    } while(0)

constexpr double kPi = 3.14159265358979323846;

struct Rig {
    std::unique_ptr<Storage<float>> st = std::make_unique<Storage<float>>();
    Fx48<float> fx;
    bool ok = false;
    explicit Rig(unsigned program, float p1 = .5f, float p2 = .5f) {
        uint32_t words[kProgramWords];
        fv1_fw::BuildBuiltin(program, words);
        ok = fx.Prepare({48000, 48}, *st);
        fx.LoadProgram(words);
        Parameters p; p.pot[1] = p1; p.pot[2] = p2;
        fx.SetParameters(p);
    }
    std::vector<float> Run(const std::vector<float>& in) {
        std::vector<float> out(in.size()), right(in.size());
        for(std::size_t i = 0; i + 48 <= in.size(); i += 48)
            fx.Process({&in[i], &in[i], &out[i], &right[i], 48});
        return out;
    }
};

// Thru program, sine in: steady-state output/input RMS ratio.
static double SineGain(double hz) {
    Rig rig(0);
    std::vector<float> in(48 * 400);
    for(std::size_t i = 0; i < in.size(); ++i) in[i] = float(.5 * std::sin(2 * kPi * hz * double(i) / 48000));
    const auto out = rig.Run(in);
    double si = 0, so = 0;
    for(std::size_t i = 4800; i < in.size(); ++i) { si += double(in[i]) * in[i]; so += double(out[i]) * out[i]; }
    return std::sqrt(so / si);
}

static void TestContract() {
    auto st = std::make_unique<Storage<float>>();
    Fx48<float> fx;
    CHECK(!fx.Prepare({44100, 48}, *st), "rejects 44.1 kHz");
    CHECK(!fx.Prepare({48000, 47}, *st), "rejects blocks that are no multiple of 3");
    CHECK(!fx.Prepare({48000, 96}, *st), "rejects blocks above 48");
    CHECK(fx.Prepare({48000, 48}, *st), "accepts 48 kHz / 48");
    float a[48] = {}, b[48] = {}, c[48] = {}, d[48] = {};
    CHECK(!fx.Process({a, b, c, d, 47}), "rejects 47 frames");
    CHECK(fx.Process({a, b, c, d, 48}) && fx.Process({a, b, c, d, 3}) && fx.Process({a, b, c, d, 0}), "accepts 48, 3, 0");
    CHECK(!fx.Process({a, b, c, c, 48}), "rejects aliased outputs");
}

static void TestPassband() {
    const double g1 = SineGain(1000), g10 = SineGain(10000), g12 = SineGain(12000);
    std::printf("thru gain: 1 kHz %.4f, 10 kHz %.4f, 12 kHz %.4f\n", g1, g10, g12);
    CHECK(std::fabs(g1 - 1) < .005, "1 kHz unity");
    CHECK(std::fabs(20 * std::log10(g10)) < .2, "10 kHz within 0.2 dB");
    CHECK(std::fabs(20 * std::log10(g12)) < 1, "12 kHz within 1 dB");
}

static void TestAliasRejection() {
    // 20 kHz would fold to 12 kHz at 32 kHz; 17.5 kHz to 14.5 kHz.
    const double a20 = 20 * std::log10(SineGain(20000)), a175 = 20 * std::log10(SineGain(17500));
    std::printf("thru rejection: 20 kHz %.1f dB, 17.5 kHz %.1f dB\n", a20, a175);
    CHECK(a20 < -60, "20 kHz rejected by more than 60 dB");
    CHECK(a175 < -40, "17.5 kHz rejected by more than 40 dB");
}

static void TestEchoTiming() {
    Rig rig(1, 0.f, 1.f); // no feedback, full wet
    std::vector<float> in(48 * 600, 0.f);
    in[480] = .5f;
    const auto out = rig.Run(in);
    std::size_t direct = 0, echo = 0;
    for(std::size_t i = 0; i < 2000; ++i) if(std::fabs(out[i]) > std::fabs(out[direct])) direct = i;
    for(std::size_t i = 20000; i < out.size(); ++i) if(std::fabs(out[i]) > std::fabs(out[echo])) echo = i;
    const double expected = 16383 * 1.5; // 16383 samples at 32 kHz in 48 kHz samples
    std::printf("dry peak %zu, echo after %zu samples (expected %.1f)\n", direct, echo - direct, expected);
    CHECK(std::fabs(double(echo - direct) - expected) <= 1.0, "echo delay keeps its 32 kHz length");
    CHECK(direct > 480 && direct < 480 + 60, "adapter latency below 60 samples");
}

int main() {
    TestContract();
    TestPassband();
    TestAliasRejection();
    TestEchoTiming();
    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("fv1-vm 48 kHz adapter tests: all passed\n");
    return 0;
}
