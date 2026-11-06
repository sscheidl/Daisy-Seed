// SPDX-License-Identifier: MIT
// Host tests for the variable-rate 48 kHz adapter (include/fv1vm/fv1_fxvar.h) with the own demo programs.
#include "fv1vm/fv1_fxvar.h"
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
    std::unique_ptr<FxVar<float>> fx = std::make_unique<FxVar<float>>();
    explicit Rig(unsigned program, float rate, float p1 = .5f, float p2 = .5f) {
        uint32_t words[kProgramWords];
        fv1_fw::BuildBuiltin(program, words);
        fx->Prepare({48000, 48}, *st);
        fx->LoadProgram(words);
        Parameters p; p.pot[1] = p1; p.pot[2] = p2;
        fx->SetParameters(p);
        fx->Reset(); // pots settled
        fx->SetRate(rate);
        float z[48] = {}, o1[48], o2[48];
        for(int i = 0; i < 100; ++i) fx->Process({z, z, o1, o2, 48}); // let the rate glide settle (silence)
    }
    std::vector<float> Run(const std::vector<float>& in) {
        std::vector<float> out(in.size()), right(in.size());
        for(std::size_t i = 0; i + 48 <= in.size(); i += 48) fx->Process({&in[i], &in[i], &out[i], &right[i], 48});
        return out;
    }
};

static double SineGain(double rate, double hz) {
    Rig rig(0, float(rate));
    std::vector<float> in(48 * 600);
    for(std::size_t i = 0; i < in.size(); ++i) in[i] = float(.5 * std::sin(2 * kPi * hz * double(i) / 48000));
    const auto out = rig.Run(in);
    double si = 0, so = 0;
    for(std::size_t i = 9600; i < in.size(); ++i) { si += double(in[i]) * in[i]; so += double(out[i]) * out[i]; }
    return std::sqrt(so / si);
}
static double Db(double g) { return 20 * std::log10(g + 1e-12); }

static void TestContract() {
    auto st = std::make_unique<Storage<float>>();
    auto fx = std::make_unique<FxVar<float>>();
    CHECK(!fx->Prepare({44100, 48}, *st) && !fx->Prepare({48000, 64}, *st), "rejects 44.1 kHz and blocks above 48");
    CHECK(fx->Prepare({48000, 48}, *st), "accepts 48 kHz / 48");
    float a[48] = {}, b[48] = {}, c[48] = {}, d[48] = {};
    CHECK(fx->Process({a, b, c, d, 47}) && fx->Process({a, b, c, d, 1}) && fx->Process({a, b, c, d, 0}), "any block length");
    CHECK(!fx->Process({a, b, c, c, 48}), "rejects aliased outputs");
    fx->SetRate(1000.f);
    for(int i = 0; i < 100; ++i) fx->Process({a, b, c, d, 48});
    CHECK(std::fabs(fx->Rate() - FxVar<float>::kMinRate) < 1e-3f, "rate clamped to 8 kHz");
}

static void TestResponse() {
    const double g32a = Db(SineGain(32000, 1000)), g32b = Db(SineGain(32000, 9000)), a32 = Db(SineGain(32000, 20000));
    const double g8a = Db(SineGain(8000, 500)), g8b = Db(SineGain(8000, 2500)), a8 = Db(SineGain(8000, 6000));
    std::printf("32 kHz: 1 kHz %.3f dB, 9 kHz %.3f dB, 20 kHz %.1f dB | 8 kHz: 500 Hz %.3f dB, 2.5 kHz %.3f dB, 6 kHz %.1f dB\n",
                g32a, g32b, a32, g8a, g8b, a8);
    CHECK(std::fabs(g32a) < .05 && std::fabs(g32b) < .5, "32 kHz passband to 9 kHz");
    CHECK(a32 < -50, "32 kHz: 20 kHz rejected by more than 50 dB");
    CHECK(std::fabs(g8a) < .05 && std::fabs(g8b) < .5, "8 kHz passband to 2.5 kHz");
    CHECK(a8 < -50, "8 kHz: 6 kHz rejected by more than 50 dB");
}

static void TestEchoAndRate() {
    Rig rig(1, 16000.f, 0.f, 1.f); // echo of 16383 interpreter samples, no feedback, full wet
    std::vector<float> in(48 * 1300, 0.f);
    in[4800] = .5f;
    const uint64_t before = rig.fx->InterpreterSamples();
    const auto out = rig.Run(in);
    const double vm_rate = double(rig.fx->InterpreterSamples() - before) / (double(in.size()) / 48000.0);
    std::size_t direct = 0, echo = 20000;
    for(std::size_t i = 0; i < 20000; ++i) if(std::fabs(out[i]) > std::fabs(out[direct])) direct = i;
    for(std::size_t i = 20000; i < out.size(); ++i) if(std::fabs(out[i]) > std::fabs(out[echo])) echo = i;
    const double expected = 16383.0 * 3.0; // 16 kHz -> 3 host samples per interpreter sample
    std::printf("16 kHz: interpreter %.1f samples/s, dry peak %zu, echo after %zu (expected %.0f)\n",
                vm_rate, direct, echo - direct, expected);
    CHECK(std::fabs(vm_rate - 16000) < 20, "interpreter runs at 16 kHz");
    CHECK(std::fabs(double(echo - direct) - expected) <= 3, "echo length scales with the rate");
    CHECK(direct > 4800 && direct < 4800 + 200, "latency below 200 samples at 16 kHz");
}

// A slow sweep 32 -> 8 -> 32 kHz with a 300 Hz sine: bounded, no jumps larger than the signal slope allows.
static void TestSweep() {
    Rig rig(0, 32000.f);
    float in[48], out[48], r[48];
    double phase = 0, worst_step = 0, peak = 0;
    float last = 0;
    for(int block = 0; block < 6000; ++block) {
        const double x = block < 3000 ? block / 3000.0 : (6000 - block) / 3000.0;
        rig.fx->SetRate(float(32000.0 * std::pow(2.0, -2.0 * x)));
        for(int i = 0; i < 48; ++i) { in[i] = float(.5 * std::sin(phase)); phase += 2 * kPi * 300 / 48000; }
        rig.fx->Process({in, in, out, r, 48});
        for(int i = 0; i < 48; ++i) {
            if(!std::isfinite(out[i])) { worst_step = 1e9; break; }
            if(block > 10) worst_step = std::fmax(worst_step, std::fabs(out[i] - last));
            peak = std::fmax(peak, std::fabs(out[i]));
            last = out[i];
        }
    }
    std::printf("sweep: peak %.3f, largest sample step %.4f (sine slope limit %.4f)\n", peak, worst_step, .5 * 2 * kPi * 300 / 48000);
    CHECK(peak < .6 && peak > .4, "level kept during the sweep");
    CHECK(worst_step < 1.07 * .5 * 2 * kPi * 300 / 48000, "no jumps during the sweep (within 7 % of the sine slope)");
}

int main() {
    TestContract();
    TestResponse();
    TestEchoAndRate();
    TestSweep();
    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("fv1-vm variable-rate adapter tests: all passed\n");
    return 0;
}
