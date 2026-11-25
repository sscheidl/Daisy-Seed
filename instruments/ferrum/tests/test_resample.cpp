// SPDX-License-Identifier: MIT
// Host tests for the 44.1 -> 48 kHz loop converter.
#include "resample.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace looper;
namespace rs = looper::resample;

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

constexpr double kPi = 3.14159265358979323846;
static std::vector<float> table(rs::kTableSize);

// Converts one loop period, feeding the input in pieces of `piece` frames.
static std::vector<Frame> Convert(const std::vector<Frame>& in, uint32_t piece = 1000) {
    const uint32_t n = uint32_t(in.size());
    const uint32_t count = n < rs::kHalf ? n : rs::kHalf;
    std::vector<Frame> out(rs::OutputFrames(n));
    rs::Upsampler u;
    CHECK(u.Start(table.data(), out.data(), n, in.data() + (n - count), count));
    for (uint32_t i = 0; i < n; i += piece)
        u.Push(in.data() + i, std::min(piece, n - i));
    CHECK(u.Finish());
    CHECK(u.Produced() == out.size());
    return out;
}

static std::vector<Frame> Sine(uint32_t frames, double hz, double rate, double amplitude = .5) {
    std::vector<Frame> v(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        const float x = float(amplitude * std::sin(2.0 * kPi * hz * i / rate));
        v[i] = {x, -x};
    }
    return v;
}

// Amplitude of the component at `hz` (an exact number of cycles in the buffer).
static double Level(const std::vector<Frame>& v, double hz, double rate) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        re += v[i].left * std::cos(2.0 * kPi * hz * double(i) / rate);
        im += v[i].left * std::sin(2.0 * kPi * hz * double(i) / rate);
    }
    return 2.0 * std::sqrt(re * re + im * im) / double(v.size());
}

static double Db(double x) { return 20.0 * std::log10(x > 1e-12 ? x : 1e-12); }

static void TestLengthAndDc() {
    CHECK(rs::OutputFrames(44100) == 48000 && rs::OutputFrames(147) == 160);
    CHECK(rs::OutputFrames(1) == 1 && rs::OutputFrames(146) == 158);
    std::vector<Frame> dc(44100, Frame{.5f, -.25f});
    double worst = 0.0;
    for (const Frame& f : Convert(dc))
        worst = std::max({worst, std::fabs(f.left - .5), std::fabs(f.right + .25)});
    std::printf("  DC: max error %.2e\n", worst);
    CHECK(worst < 1e-5);
}

// One second at 44.1 kHz holds whole cycles, so the loop is periodic and the
// output must match the same sine sampled at 48 kHz, including at the seam.
static void TestSineAccuracy() {
    for (const double hz : {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0}) {
        const std::vector<Frame> out = Convert(Sine(44100, hz, 44100.0));
        const std::vector<Frame> ideal = Sine(48000, hz, 48000.0);
        double worst = 0.0, seam = 0.0;
        for (size_t i = 0; i < out.size(); ++i) {
            const double e = std::fabs(double(out[i].left) - ideal[i].left);
            worst = std::max(worst, e);
            if (i < 64 || i + 64 >= out.size())
                seam = std::max(seam, e);
            CHECK(out[i].right == -out[i].left);
        }
        std::printf("  %6.0f Hz: max error %6.1f dB (seam %6.1f dB) re 0.5\n", hz, Db(worst / .5),
                    Db(seam / .5));
        CHECK(Db(worst / .5) < (hz <= 15000.0 ? -70.0 : -60.0));
        CHECK(seam <= worst);
    }
}

// A 15 kHz tone leaves an image at 44.1 - 15 = 29.1 kHz, which would fold to
// 48 - 29.1 = 18.9 kHz in the output. The filter must suppress it.
static void TestImages() {
    for (const double hz : {12000.0, 15000.0, 17000.0}) {
        const std::vector<Frame> out = Convert(Sine(44100, hz, 44100.0));
        const double image = 48000.0 - (44100.0 - hz);
        const double wanted = Level(out, hz, 48000.0), alias = Level(out, image, 48000.0);
        std::printf("  %5.0f Hz: passband %+.3f dB, image at %5.0f Hz %6.1f dB\n", hz, Db(wanted / .5),
                    image, Db(alias / .5));
        CHECK(std::fabs(Db(wanted / .5)) < .1);
        CHECK(Db(alias / .5) < -70.0);
    }
    // Above the passband the filter rolls off toward the 22.05 kHz input Nyquist.
    for (const double hz : {19000.0, 20000.0, 21000.0})
        std::printf("  %5.0f Hz: %+.2f dB\n", hz, Db(Level(Convert(Sine(44100, hz, 44100.0)), hz, 48000.0) / .5));
}

static void TestShortLoopsAndChunking() {
    for (const uint32_t n : {1u, 2u, 5u, 31u, 32u, 33u, 64u, 147u, 1000u}) {
        std::vector<Frame> in(n);
        for (uint32_t i = 0; i < n; ++i)
            in[i] = {float(int(i * 37 % 19) - 9) / 10.f, .3f};
        const std::vector<Frame> a = Convert(in, 1), b = Convert(in, 7), c = Convert(in, 4096);
        CHECK(a.size() == rs::OutputFrames(n));
        bool same = a.size() == b.size() && a.size() == c.size(), finite = true;
        for (size_t i = 0; i < a.size(); ++i) {
            same = same && a[i].left == b[i].left && a[i].left == c[i].left;
            finite = finite && std::isfinite(a[i].left) && std::fabs(a[i].right - .3f) < 1e-5f;
        }
        CHECK(same && finite);
    }
    // Wrong history or missing input is rejected.
    std::vector<Frame> out(200), in(100);
    rs::Upsampler u;
    CHECK(!u.Start(table.data(), out.data(), 100, in.data(), 5));
    CHECK(!u.Start(nullptr, out.data(), 100, in.data(), rs::kHalf));
    CHECK(!u.Start(table.data(), out.data(), 0, in.data(), 1));
    CHECK(u.Start(table.data(), out.data(), 100, in.data(), rs::kHalf));
    u.Push(in.data(), 50);
    CHECK(!u.Finish());
}

// A full-scale square wave rings past +-1; the output is clamped.
static void TestClamp() {
    std::vector<Frame> square(44100);
    for (uint32_t i = 0; i < square.size(); ++i)
        square[i].left = square[i].right = (i / 441) % 2 ? 1.f : -1.f;
    float peak = 0.f;
    for (const Frame& f : Convert(square))
        peak = std::max(peak, std::fabs(f.left));
    CHECK(peak <= 1.f && peak > .99f);
}

int main() {
    rs::BuildTable(table.data());
    TestLengthAndDc();
    TestSineAccuracy();
    TestImages();
    TestShortLoopsAndChunking();
    TestClamp();
    if (failures) {
        std::printf("FAILED: %d resampler check(s)\n", failures);
        return 1;
    }
    std::printf("All resampler tests passed\n");
    return 0;
}
