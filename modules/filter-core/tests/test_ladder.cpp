// SPDX-License-Identifier: MIT
// Host tests of the filter core: response per mode against the exact closed-loop formula,
// stability at maximum resonance under full modulation, calibrated self-oscillation,
// bass compensation, ramping, the held path, denormals, NaN, and the golden hash.
#include "filter_core/ladder.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace seed::dsp::filter;
using Complex = std::complex<double>;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kFs = kSampleRate;
static constexpr std::size_t kBlock = 48;
static constexpr Mode kAll[kModeCount] = {Mode::Lp6, Mode::Lp12, Mode::Lp18, Mode::Lp24,
                                          Mode::Bp,  Mode::Hp,   Mode::Notch, Mode::Phaser};
static const char* const kNames[kModeCount] = {"LP6", "LP12", "LP18", "LP24", "BP", "HP", "Notch", "Phaser"};

static double NoteOf(double hz) { return 69. + 12. * std::log2(hz / 440.); }
static double Db(double gain) { return 20. * std::log10(gain + 1e-15); }

static Parameters Settings(double hz, double resonance, Mode mode, double comp = 1.) {
    Parameters p;
    p.cutoff_note = float(NoteOf(hz));
    p.resonance = float(resonance);
    p.bass_comp = float(comp);
    p.mode = mode;
    return p;
}

// Amplitude of `hz` in y by projection on a whole number of its periods.
static double Amplitude(const std::vector<float>& y, double hz) {
    const std::size_t periods = std::size_t(double(y.size()) * hz / kFs);
    const std::size_t count = periods ? std::size_t(double(periods) * kFs / hz) : y.size();
    double c = 0., s = 0.;
    for(std::size_t i = 0; i < count; ++i) {
        const double phase = 2. * kPi * hz / kFs * double(i);
        c += y[i] * std::cos(phase);
        s += y[i] * std::sin(phase);
    }
    return 2. * std::hypot(c, s) / double(count);
}

// Steady-state gain of a quiet sine; quiet so that the limiter stays out of the way.
static double MeasuredGain(const Parameters& p, double hz, double amplitude = 1e-3) {
    Ladder filter;
    filter.Set(p);
    const std::size_t settle = std::size_t(kFs * 2.), keep = std::size_t(kFs);
    std::vector<float> in(kBlock), out(kBlock), tail;
    tail.reserve(keep);
    for(std::size_t n = 0; n < settle + keep; n += kBlock) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = float(amplitude * std::sin(2. * kPi * hz / kFs * double(n + i)));
        filter.Process(in.data(), out.data(), kBlock);
        if(n >= settle)
            tail.insert(tail.end(), out.begin(), out.end());
    }
    return Amplitude(tail, hz) / amplitude;
}

// The exact response of the linear filter: bilinear one-poles, closed loop, tap mix.
static double ExpectedGain(Mode mode, double cutoff, double hz, double resonance, double comp) {
    const double g = std::tan(kPi * cutoff / kFs);
    const Complex w = g / (g + Complex(0., std::tan(kPi * hz / kFs)));
    const double k = (mode == Mode::Notch ? double(kNotchFeedback) : double(kMaxFeedback)) * resonance;
    const Complex loop = 1. / (1. + k * w * w * w * w), w2 = w * w, w3 = w2 * w, w4 = w2 * w2;
    switch(mode) {
        case Mode::Lp6: return std::abs((1. + comp * k) * w * loop);
        case Mode::Lp12: return std::abs((1. + comp * k) * w2 * loop);
        case Mode::Lp18: return std::abs((1. + comp * k) * w3 * loop);
        case Mode::Lp24: return std::abs((1. + comp * k) * w4 * loop);
        case Mode::Bp: return std::abs(4. * w2 * (1. - w) * (1. - w) * loop);
        case Mode::Hp: return std::abs((1. - w) * (1. - w) * (1. - w) * (1. - w) * loop);
        case Mode::Notch: return std::abs(1. - 2. * (1. - 0.25 * k) * w * (1. - w) * loop);
        case Mode::Phaser: {
            const Complex ap = 2. * w - 1.;
            return std::abs((0.5 * (1. + ap * ap * ap * ap) + comp * k * (2. * w3 - 2. * w2 + w)) * loop);
        }
    }
    return 0.;
}

static void TestTable() {
    // The cutoff table against libm: within a fraction of a cent over the whole range.
    double worst = 0.;
    for(float note = 12.f; note <= 135.f; note += 0.37f) {
        const double G = double(LookUp(note).G), g = G / (1. - G);
        const double hz = kFs / kPi * std::atan(g);
        worst = std::max(worst, std::fabs(1200. * std::log2(hz / (440. * std::exp2((double(note) - 69.) / 12.)))));
    }
    std::printf("note table: largest pitch error %.3f cents\n", worst);
    CHECK(worst < 0.5);
    // Clamping: NaN and absurd notes stay on the table.
    CHECK(LookUp(std::nanf("")).G == kNoteTable.rows[0].G);
    CHECK(LookUp(-1e30f).G == kNoteTable.rows[0].G);
    CHECK(LookUp(1e30f).G == LookUp(kNoteMax).G);
}

static void TestResponse() {
    static const double cutoffs[] = {200., 1000., 5000.};
    static const double offsets[] = {0.1, 0.25, 0.5, 0.71, 1., 1.41, 2., 4., 10.};
    double worst = 0.;
    for(unsigned m = 0; m < kModeCount; ++m)
        for(double cutoff : cutoffs)
            for(double resonance : {0., 0.45, 0.8})
                for(double comp : {0., 1.})
                    for(double offset : offsets) {
                        const double hz = cutoff * offset;
                        if(hz > 20000.)
                            continue;
                        const double expected = Db(ExpectedGain(kAll[m], cutoff, hz, resonance, comp));
                        if(expected < -70.) // deep stopbands and notches: tested below
                            continue;
                        const double measured = Db(MeasuredGain(Settings(cutoff, resonance, kAll[m], comp), hz));
                        const double error = std::fabs(measured - expected);
                        worst = std::max(worst, error);
                        if(error > 0.25)
                            std::printf("  %s fc %.0f r %.2f c %.0f at %.0f Hz: expected %.2f, measured %.2f\n",
                                        kNames[m], cutoff, resonance, comp, hz, expected, measured);
                        CHECK(error <= 0.25);
                    }
    std::printf("response against the closed-loop formula: largest error %.3f dB\n", worst);

    // Slopes at resonance 0, well below Nyquist, where the bilinear zero does not steepen them.
    const double fc = 100.;
    const auto per_octave = [&](Mode mode, double a, double b) {
        return (Db(MeasuredGain(Settings(fc, 0., mode), a, 0.1)) - Db(MeasuredGain(Settings(fc, 0., mode), b, 0.1))) /
               std::fabs(std::log2(b / a));
    };
    const double lp6 = per_octave(Mode::Lp6, 800., 1600.), lp12 = per_octave(Mode::Lp12, 800., 1600.);
    const double lp18 = per_octave(Mode::Lp18, 800., 1600.), lp24 = per_octave(Mode::Lp24, 800., 1600.);
    const double hp = per_octave(Mode::Hp, 12.5, 6.25), bp_high = per_octave(Mode::Bp, 800., 1600.);
    const double bp_low = per_octave(Mode::Bp, 12.5, 6.25);
    std::printf("slopes dB/oct: LP %.1f %.1f %.1f %.1f, HP %.1f, BP %.1f / %.1f\n", lp6, lp12, lp18, lp24, hp, bp_low,
                bp_high);
    CHECK(std::fabs(lp6 - 6.) < 0.3 && std::fabs(lp12 - 12.) < 0.5);
    CHECK(std::fabs(lp18 - 18.) < 0.7 && std::fabs(lp24 - 24.) < 1.);
    CHECK(std::fabs(hp - 24.) < 1. && std::fabs(bp_low - 12.) < 0.5 && std::fabs(bp_high - 12.) < 0.5);
    // Resonance raises the peak at the cutoff, and the cutoff tracks in every position.
    for(double cutoff : {100., 1000., 10000.}) {
        const double peak = Db(MeasuredGain(Settings(cutoff, 0.8, Mode::Lp24, 0.), cutoff));
        CHECK(std::fabs(peak - Db(ExpectedGain(Mode::Lp24, cutoff, cutoff, 0.8, 0.))) < 0.25);
    }
    // Notch: deep at the cutoff at every resonance, flat and at unity on both sides. The
    // narrow notch at 100 % is limited by the table's fraction of a cent.
    for(double resonance : {0., 0.5, 1.}) {
        const double depth = Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 1000., 0.1));
        std::printf("notch depth at resonance %.1f: %.1f dB\n", resonance, depth);
        CHECK(depth < -35.);
        CHECK(std::fabs(Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 30.))) < 0.5);
        CHECK(std::fabs(Db(MeasuredGain(Settings(1000., resonance, Mode::Notch), 20000.))) < 1.);
    }
    // Phaser: two notches around a passing centre.
    const double low = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 414.2));
    const double high = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 2414.));
    const double centre = Db(MeasuredGain(Settings(1000., 0., Mode::Phaser), 1000.));
    std::printf("phaser: notches %.1f / %.1f dB, centre %.1f dB\n", low, high, centre);
    CHECK(low < -30. && high < -30. && std::fabs(centre) < 0.5);
    // Full scale at resonance 0: the limiter stands back, so the stopband stays deep.
    const double stopband = Db(MeasuredGain(Settings(1000., 0., Mode::Hp), 62.5, 1.));
    std::printf("HP stopband four octaves down at full scale: %.1f dB\n", stopband);
    CHECK(stopband < -85.);
}

static void TestBassCompensation() {
    // DC gain (1 + c k) / (1 + k) of the low-passes at the top of the linear range.
    const double resonance = 0.9, k = double(kMaxFeedback) * resonance;
    for(Mode mode : {Mode::Lp6, Mode::Lp12, Mode::Lp18, Mode::Lp24})
        for(double c : {0., 0.5, 1.}) {
            const double gain = MeasuredGain(Settings(2000., resonance, mode, c), 20.);
            CHECK(std::fabs(Db(gain) - Db((1. + c * k) / (1. + k))) < 0.1);
        }
    // Modes without a low-frequency passband ignore c.
    for(Mode mode : {Mode::Hp, Mode::Bp, Mode::Notch})
        CHECK(std::fabs(Db(MeasuredGain(Settings(2000., 0.5, mode, 0.), 5000.)) -
                        Db(MeasuredGain(Settings(2000., 0.5, mode, 1.), 5000.))) < 0.01);
    // Phaser: c restores the bass.
    CHECK(Db(MeasuredGain(Settings(2000., 0.8, Mode::Phaser, 0.), 50.)) < -10.);
    CHECK(std::fabs(Db(MeasuredGain(Settings(2000., 0.8, Mode::Phaser, 1.), 50.))) < 0.5);
}

// Runs an oscillator from silence and returns the fundamental level and frequency.
struct Oscillation {
    double level_db, hz;
};
static Oscillation Oscillate(double hz, Mode mode, bool held = false) {
    Ladder filter;
    filter.Set(Settings(hz, 1., mode));
    const std::size_t total = std::size_t(kFs * std::max(3., 150. / hz + 1.));
    const std::size_t tail = std::size_t(kFs / 2.);
    std::vector<float> in(kBlock, 0.f), out(kBlock), last;
    last.reserve(tail + kBlock);
    for(std::size_t n = 0; n < total; n += kBlock) {
        if(held)
            filter.ProcessHeld(in.data(), out.data(), kBlock);
        else
            filter.Process(in.data(), out.data(), kBlock);
        if(n + tail >= total)
            last.insert(last.end(), out.begin(), out.end());
    }
    double first = -1., latest = 0.;
    int crossings = 0;
    for(std::size_t i = 1; i < last.size(); ++i)
        if(last[i - 1] <= 0.f && last[i] > 0.f) {
            latest = double(i - 1) + double(-last[i - 1]) / double(last[i] - last[i - 1]);
            first = first < 0. ? latest : first;
            ++crossings;
        }
    const double f = crossings > 1 ? kFs * double(crossings - 1) / (latest - first) : 0.;
    double c = 0., s = 0.; // the crossings give the frequency exactly enough for a projection
    for(std::size_t i = 0; i < last.size(); ++i) {
        c += last[i] * std::cos(2. * kPi * f / kFs * double(i));
        s += last[i] * std::sin(2. * kPi * f / kFs * double(i));
    }
    return {Db(2. * std::hypot(c, s) / double(last.size()) / double(kOscLevel)), f};
}

static void TestSelfOscillation() {
    static const double cutoffs[] = {30., 125., 500., 2000., 8000., 16000.};
    double lo = 1e9, hi = -1e9, tune = 0.;
    for(double hz : cutoffs)
        for(Mode mode : kAll) {
            if(mode == Mode::Notch)
                continue;
            const Oscillation o = Oscillate(hz, mode);
            lo = std::min(lo, o.level_db);
            hi = std::max(hi, o.level_db);
            CHECK(std::fabs(o.level_db) < 1.);
            if(hz <= 8000.) {
                tune = std::max(tune, std::fabs(1200. * std::log2(o.hz / hz)));
                CHECK(std::fabs(1200. * std::log2(o.hz / hz)) < 10.);
            }
        }
    std::printf("self-oscillation: level %.2f .. %.2f dB around %.2f peak, pitch within %.1f cents to 8 kHz\n", lo, hi,
                double(kOscLevel), tune);
    // The held path uses the same coefficients and oscillates the same.
    const Oscillation ramped = Oscillate(1000., Mode::Lp24), held = Oscillate(1000., Mode::Lp24, true);
    CHECK(std::fabs(ramped.level_db - held.level_db) < 0.05);
    CHECK(std::fabs(1200. * std::log2(ramped.hz / held.hz)) < 1.);
    // Notch: the zero sits on the pole, and the loop stays below 4. No oscillation by design.
    CHECK(Oscillate(1000., Mode::Notch).level_db < -60.);
    // Below the threshold nothing rings on.
    {
        Ladder filter;
        filter.Set(Settings(1000., 0.89, Mode::Lp24));
        std::vector<float> in(kBlock, 0.f), out(kBlock);
        in[0] = 1.f;
        filter.Process(in.data(), out.data(), kBlock);
        in[0] = 0.f;
        double peak = 0.;
        for(std::size_t n = 0; n < std::size_t(kFs); n += kBlock) {
            filter.Process(in.data(), out.data(), kBlock);
            if(n > std::size_t(kFs / 2.))
                for(float v : out)
                    peak = std::max(peak, double(std::fabs(v)));
        }
        CHECK(peak < 1e-3);
    }
}

// White noise at full scale, resonance 100 %, and random cutoff jumps over the full range
// on every sample and every block, in every mode, on both paths.
static void TestStability() {
    uint32_t seed = 12345u;
    const auto noise = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(int32_t(seed)) / 2147483648.f;
    };
    double worst = 0.;
    std::vector<float> in(kBlock), out(kBlock), fm(kBlock);
    for(Mode mode : kAll)
        for(double amplitude : {1., 16.})
            for(int path = 0; path < 3; ++path) {
                Ladder filter;
                double peak = 0.;
                for(std::size_t n = 0; n < std::size_t(kFs * 4.); n += kBlock) {
                    Parameters p = Settings(1000., 1., mode);
                    p.cutoff_note = 67.5f + 67.5f * noise();
                    filter.Set(p);
                    for(std::size_t i = 0; i < kBlock; ++i) {
                        in[i] = float(amplitude) * noise();
                        fm[i] = 135.f * noise();
                    }
                    if(path == 0)
                        filter.Process(in.data(), out.data(), kBlock);
                    else if(path == 1)
                        filter.Process(in.data(), out.data(), kBlock, fm.data());
                    else
                        filter.ProcessHeld(in.data(), out.data(), 8);
                    for(std::size_t i = 0; i < (path == 2 ? 8u : kBlock); ++i) {
                        CHECK(std::isfinite(out[i]));
                        peak = std::max(peak, double(std::fabs(out[i])));
                    }
                }
                for(int i = 0; i < 4; ++i)
                    CHECK(std::isfinite(filter.State()[i]));
                worst = std::max(worst, peak / amplitude);
            }
    std::printf("stability: largest |out| / input peak %.1f under full modulation\n", worst);
    CHECK(worst < 64.);
    // The same at a fixed cutoff with a loud tone at resonance: the limiter must hold it.
    for(double hz : {40., 1000., 15000.}) {
        Ladder filter;
        filter.Set(Settings(hz, 1., Mode::Lp24));
        double peak = 0.;
        for(std::size_t n = 0; n < std::size_t(kFs * 2.); n += kBlock) {
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = float(std::sin(2. * kPi * hz / kFs * double(n + i)));
            filter.Process(in.data(), out.data(), kBlock);
            for(float v : out)
                peak = std::max(peak, double(std::fabs(v)));
        }
        std::printf("full-scale tone at the oscillating cutoff %.0f Hz: output peak %.2f\n", hz, peak);
        CHECK(peak < 4.);
    }
}

static bool Subnormal(float x) { return x != 0.f && std::fabs(x) < 1.17549435e-38f; }

static void TestDenormalsAndNan() {
    // After a signal ends, the state must never decay into the subnormal range.
    for(Mode mode : kAll)
        for(double hz : {20., 1000., 19000.}) {
            Ladder filter;
            filter.Set(Settings(hz, 0.5, mode));
            std::vector<float> in(kBlock, 0.5f), out(kBlock);
            filter.Process(in.data(), out.data(), kBlock);
            std::fill(in.begin(), in.end(), 0.f);
            bool clean = true;
            for(std::size_t n = 0; n < std::size_t(kFs * 30.); n += kBlock) {
                filter.Process(in.data(), out.data(), kBlock);
                for(int i = 0; i < 4; ++i)
                    clean &= !Subnormal(filter.State()[i]);
                for(float v : out)
                    clean &= !Subnormal(v);
            }
            CHECK(clean);
        }
    // Non-finite input counts as silence and leaves a working filter.
    std::vector<float> bad = {std::nanf(""), INFINITY, -INFINITY, 1e30f, -1e30f, 0.f, 1.f, 0.f};
    for(Mode mode : kAll) {
        Ladder filter;
        filter.Set(Settings(1000., 1., mode));
        std::vector<float> out(bad.size());
        for(int i = 0; i < 1000; ++i) {
            filter.Process(bad.data(), out.data(), bad.size());
            for(float v : out)
                CHECK(std::isfinite(v));
        }
        // Non-finite parameters too.
        Parameters p = Settings(1000., 0.5, mode);
        p.cutoff_note = std::nanf("");
        p.resonance = std::nanf("");
        p.bass_comp = INFINITY;
        filter.Set(p);
        filter.Process(bad.data(), out.data(), bad.size());
        for(float v : out)
            CHECK(std::isfinite(v));
        std::vector<float> fm(bad);
        filter.Process(bad.data(), out.data(), bad.size(), fm.data());
        for(float v : out)
            CHECK(std::isfinite(v));
    }
}

// RMS of the fourth difference: a steep high-pass. Smooth motion gives almost nothing,
// while steps in the coefficients leave broadband zipper noise.
static double Zipper(const std::vector<float>& y) {
    double sum = 0.;
    for(std::size_t i = 4; i < y.size(); ++i) {
        const double d = double(y[i]) - 4. * double(y[i - 1]) + 6. * double(y[i - 2]) - 4. * double(y[i - 3]) + double(y[i - 4]);
        sum += d * d;
    }
    return std::sqrt(sum / double(y.size()));
}

static void TestRamping() {
    // A fast cutoff sweep over a low tone: steps in the coefficients leave zipper noise.
    const auto sweep = [](bool held) {
        Ladder filter;
        std::vector<float> in(kBlock, 0.5f), out(kBlock), all;
        for(int block = 0; block < 400; ++block) {
            Parameters p = Settings(1000., 0.6, Mode::Lp6, 0.);
            p.cutoff_note = 60.f + 36.f * float(std::sin(2. * kPi * double(block) / 100.));
            filter.Set(p);
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = 0.5f * float(std::sin(2. * kPi * 300. / kFs * double(block * 48 + int(i))));
            if(held)
                filter.ProcessHeld(in.data(), out.data(), kBlock);
            else
                filter.Process(in.data(), out.data(), kBlock);
            if(block > 20)
                all.insert(all.end(), out.begin(), out.end());
        }
        return Zipper(all);
    };
    const double ramped = sweep(false), held = sweep(true);
    std::printf("cutoff sweep, zipper residue: ramped %.1f dB, held per block %.1f dB\n", Db(ramped), Db(held));
    CHECK(ramped * 4. < held);
    // Mode changes do not step: the mix moves over kSlowMs.
    for(Mode a : kAll)
        for(Mode b : kAll) {
            Ladder filter;
            filter.Set(Settings(1000., 0.3, a));
            std::vector<float> in(kBlock), out(kBlock);
            double previous = 0., largest = 0.;
            for(int block = 0; block < 200; ++block) {
                if(block == 100)
                    filter.Set(Settings(1000., 0.3, b));
                for(std::size_t i = 0; i < kBlock; ++i)
                    in[i] = 0.25f * float(std::sin(2. * kPi * 100. / kFs * double(block * 48 + int(i))));
                filter.Process(in.data(), out.data(), kBlock);
                for(float v : out) {
                    if(block >= 50)
                        largest = std::max(largest, std::fabs(double(v) - previous));
                    previous = double(v);
                }
            }
            CHECK(largest < 0.1);
        }
    // Audio-rate input: an FM buffer at 1 kHz puts sidebands on a 500 Hz tone through LP24
    // with the cutoff near the tone, and matches a per-sample note sweep of the same shape.
    {
        Ladder with_fm, reference;
        with_fm.Set(Settings(800., 0.5, Mode::Lp24));
        std::vector<float> in(kBlock), out(kBlock), fm(kBlock), tail;
        for(int block = 0; block < 2000; ++block) {
            for(std::size_t i = 0; i < kBlock; ++i) {
                in[i] = 0.1f * float(std::sin(2. * kPi * 500. / kFs * double(block * 48 + int(i))));
                fm[i] = 12.f * float(std::sin(2. * kPi * 1000. / kFs * double(block * 48 + int(i))));
            }
            with_fm.Process(in.data(), out.data(), kBlock, fm.data());
            if(block >= 1000)
                tail.insert(tail.end(), out.begin(), out.end());
        }
        const double carrier = Amplitude(tail, 500.), sideband = Amplitude(tail, 1500.);
        std::printf("audio-rate cutoff input: carrier %.1f dB, 1500 Hz sideband %.1f dB\n", Db(carrier), Db(sideband));
        CHECK(sideband > carrier * 0.02);
        // The same call with one sample per call and the note in Set is the same filter.
        Ladder a, b;
        a.Set(Settings(800., 0.5, Mode::Lp24));
        b.Set(Settings(800., 0.5, Mode::Lp24));
        float x = 0.25f, ya = 0.f, yb = 0.f, offset = 0.f;
        double difference = 0.;
        for(int n = 0; n < 4800; ++n) {
            offset = 12.f * float(std::sin(2. * kPi * 200. / kFs * double(n)));
            a.Process(&x, &ya, 1, &offset);
            Parameters p = Settings(800., 0.5, Mode::Lp24);
            p.cutoff_note += offset;
            b.Set(p);
            b.Process(&x, &yb, 1);
            difference = std::max(difference, std::fabs(double(ya) - double(yb)));
        }
        CHECK(difference < 0.01);
    }
}

static void TestPaths() {
    // Static settings: the held path gives the same numbers as the ramped path.
    Ladder ramped, held;
    ramped.Set(Settings(700., 0.7, Mode::Lp18));
    held.Set(Settings(700., 0.7, Mode::Lp18));
    std::vector<float> in(kBlock), a(kBlock), b(kBlock);
    double difference = 0.;
    for(int block = 0; block < 500; ++block) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = 0.3f * float(std::sin(2. * kPi * 311. / kFs * double(block * 48 + int(i))));
        ramped.Process(in.data(), a.data(), kBlock);
        held.ProcessHeld(in.data(), b.data(), kBlock);
        for(std::size_t i = 0; i < kBlock; ++i)
            difference = std::max(difference, std::fabs(double(a[i]) - double(b[i])));
    }
    CHECK(difference < 1e-6);
    // In place equals out of place.
    Ladder one, two;
    one.Set(Settings(700., 0.7, Mode::Hp));
    two.Set(Settings(700., 0.7, Mode::Hp));
    for(int block = 0; block < 100; ++block) {
        for(std::size_t i = 0; i < kBlock; ++i)
            in[i] = 0.3f * float(std::sin(2. * kPi * 311. / kFs * double(block * 48 + int(i))));
        std::vector<float> copy(in);
        one.Process(copy.data(), copy.data(), kBlock);
        two.Process(in.data(), a.data(), kBlock);
        CHECK(std::equal(copy.begin(), copy.end(), a.begin()));
    }
    // Zero frames do nothing; Reset clears the state.
    one.Process(nullptr, nullptr, 0);
    one.ProcessHeld(nullptr, nullptr, 0);
    one.Reset();
    for(int i = 0; i < 4; ++i)
        CHECK(one.State()[i] == 0.f);
}

static void TestGolden() {
    // FNV-1a over the output of every mode: the sound of this version. Only + - * / and
    // comparisons run in the path, so the hash holds on every IEEE host (without contraction).
    uint32_t hash = 2166136261u;
    std::vector<float> in(kBlock), out(kBlock);
    for(Mode mode : kAll) {
        Ladder filter;
        for(int block = 0; block < 2000; ++block) {
            Parameters p = Settings(500., 0.95, mode);
            p.cutoff_note += float(block % 100) * 0.25f;
            filter.Set(p);
            for(std::size_t i = 0; i < kBlock; ++i)
                in[i] = float((block * 48 + int(i)) % 109) / 54.5f - 1.f;
            filter.Process(in.data(), out.data(), kBlock);
            for(float v : out) {
                uint32_t bits;
                std::memcpy(&bits, &v, sizeof(bits));
                for(int b = 0; b < 4; ++b) {
                    hash ^= (bits >> (8 * b)) & 0xFFu;
                    hash *= 16777619u;
                }
            }
        }
    }
    std::printf("golden hash 0x%08X\n", unsigned(hash));
    CHECK(hash == 0x150EB967u);
}

int main() {
    TestTable();
    TestResponse();
    TestBassCompensation();
    TestSelfOscillation();
    TestStability();
    TestDenormalsAndNan();
    TestRamping();
    TestPaths();
    TestGolden();
    if(failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("filter core: all tests passed\n");
    return 0;
}
