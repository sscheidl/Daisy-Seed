// SPDX-License-Identifier: MIT
// Rate comparison (docs/RATE_COMPARISON.md): the own string core running internally at
// 40 kHz (the original engine rate, SF-A05) followed by an own windowed-sinc resampler to
// 48 kHz, against the same core time-scaled to run directly at 48 kHz.
//
// Reported per test note: inharmonic (alias) energy relative to the total, measured in a
// 48 kHz FFT outside +-3 bins of the multiples of the lowest footage frequency, and the
// host time of both paths. Host times are relative costs on this PC only, not a Seed3
// measurement. Own signals; no original render is involved.
#include "streichwurst/instrument.h"
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace streichwurst;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Own polyphase resampler 40 -> 48 kHz (ratio 5/6): Kaiser-windowed sinc, 48 taps per
// phase, cutoff 0.45 x 40 kHz (below both Nyquist limits). Offline tool, double precision.
std::vector<float> Resample40To48(const std::vector<float>& in) {
    const int taps = 48, half = taps / 2;
    const double cutoff = 0.45; // relative to the 40 kHz input rate
    auto bessel0 = [](double x) {
        double sum = 1, term = 1;
        for(int k = 1; k < 30; ++k) {
            term *= (x / (2 * k)) * (x / (2 * k));
            sum += term;
        }
        return sum;
    };
    const double beta = 8.0, norm = bessel0(beta);
    std::vector<float> out;
    const std::size_t n_out = in.size() * 6 / 5;
    out.reserve(n_out);
    for(std::size_t m = 0; m < n_out; ++m) {
        const double t = double(m) * 40000.0 / 48000.0; // position in input samples
        const long base = long(std::floor(t));
        double acc = 0;
        for(int k = -half + 1; k <= half; ++k) {
            const long i = base + k;
            if(i < 0 || i >= long(in.size()))
                continue;
            const double x = t - double(i);
            const double s = x == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * x) / (kPi * x);
            const double r = x / half;
            const double w = std::fabs(r) >= 1 ? 0 : bessel0(beta * std::sqrt(1 - r * r)) / norm;
            acc += double(in[std::size_t(i)]) * s * w;
        }
        out.push_back(float(acc));
    }
    return out;
}

void Fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for(std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j)
            std::swap(a[i], a[j]);
    }
    for(std::size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2 * kPi / double(len));
        for(std::size_t i = 0; i < n; i += len) {
            std::complex<double> u = 1;
            for(std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
}

// Share of energy (dB) outside the harmonic series of `base` Hz, measured at 48 kHz.
double InharmonicDb(const std::vector<float>& x, std::size_t from, double base) {
    const std::size_t n = 16384;
    std::vector<std::complex<double>> a(n);
    for(std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n - 1));
        a[i] = x[from + i] * w;
    }
    Fft(a);
    const double bin = 48000.0 / double(n);
    double total = 0, outside = 0;
    for(std::size_t k = 2; k < n / 2; ++k) {
        const double f = double(k) * bin, e = std::norm(a[k]);
        if(f > 20000.0)
            continue; // the audible band
        total += e;
        const double h = std::round(f / base);
        if(h < 1 || std::fabs(f - h * base) > 3 * bin)
            outside += e;
    }
    return 10 * std::log10(outside / total + 1e-30);
}

struct Result {
    std::vector<float> strings;
    double seconds = 0;
};
Result Render(float fs, int note, float registration, int octave, std::size_t samples) {
    static InstrumentStorage storage;
    static Instrument inst;
    inst.Prepare(fs, 48, storage);
    InstrumentParams p;
    p.string_registration = registration;
    p.string_octave = octave;
    p.string_crescendo = 0.f;
    inst.SetParameters(p);
    Result r;
    r.strings.resize(samples);
    std::vector<float> solo(48);
    const auto t0 = std::chrono::steady_clock::now();
    for(std::size_t s = 0; s < samples; s += 48) {
        Event on = NoteOn(0, uint8_t(note), 100);
        const std::size_t n = std::min<std::size_t>(48, samples - s);
        inst.RenderSections(s == 0 ? &on : nullptr, s == 0 ? 1 : 0, SectionBuses{r.strings.data() + s, solo.data(), n});
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}
} // namespace

int main() {
    std::printf("Streichwurst string core: 40 kHz + own resampler vs. direct 48 kHz (own signals)\n");
    std::printf("%-26s %14s %14s %12s\n", "note / registration", "40k->48k [dB]", "48k [dB]", "host t40/t48");
    struct Case {
        int note;
        float registration;
        int octave;
    } cases[] = {{57, 0.5f, 0}, {69, 0.5f, 0}, {81, 0.5f, 1}, {93, 1.0f, 0}, {100, 1.0f, 1}, {108, 1.0f, 0}};
    for(const Case& c : cases) {
        const Result r40 = Render(40000.f, c.note, c.registration, c.octave, 40000 * 2);
        const Result r48 = Render(48000.f, c.note, c.registration, c.octave, 48000 * 2);
        const std::vector<float> up = Resample40To48(r40.strings);
        const double base = 440.0 * std::pow(2.0, (c.note - 12 - 69) / 12.0); // lowest footage (-12)
        const double a40 = InharmonicDb(up, 48000, base), a48 = InharmonicDb(r48.strings, 48000, base);
        char label[40];
        std::snprintf(label, sizeof(label), "%d / %.2f / oct %d", c.note, c.registration, c.octave);
        std::printf("%-26s %14.1f %14.1f %12.2f\n", label, a40, a48, r40.seconds / r48.seconds);
    }
    std::printf("Host times include no resampler; the resampler adds its own cost on the board.\n");
    return 0;
}
