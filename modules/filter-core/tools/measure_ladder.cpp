// SPDX-License-Identifier: MIT
// Measurement report of the filter core: self-oscillation level, pitch and purity per
// cutoff and mode, how soon it starts, and distortion at low resonance. It runs the real
// core and prints what it measures; the tables in docs/DESIGN.md come from this tool.
//   make -C modules/filter-core measure
#include "filter_core/ladder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace seed::dsp::filter;

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kFs = kSampleRate;
static const char* const kNames[kModeCount] = {"LP6", "LP12", "LP18", "LP24", "BP", "HP", "Notch", "Phaser"};

static double NoteOf(double hz) { return 69. + 12. * std::log2(hz / 440.); }

struct Osc {
    double peak_db, cents, thd_db, seconds; // seconds until within 1 dB of the final level
};

// Amplitude of the component at `hz` by projection.
static double Bin(const std::vector<float>& y, double hz) {
    double c = 0., s = 0.;
    for(std::size_t i = 0; i < y.size(); ++i) {
        const double phase = 2. * kPi * hz / kFs * double(i);
        c += y[i] * std::cos(phase);
        s += y[i] * std::sin(phase);
    }
    return 2. * std::hypot(c, s) / double(y.size());
}

static Osc Oscillate(double hz, Mode mode) {
    Ladder filter;
    Parameters p;
    p.cutoff_note = float(NoteOf(hz));
    p.resonance = 1.f;
    p.mode = mode;
    filter.Set(p);
    const std::size_t block = 48;
    // Slow cutoffs take seconds to reach the level from the -120 dBFS noise floor.
    const std::size_t total = std::size_t(kFs * std::max(3., 150. / hz + 1.)), tail = std::size_t(kFs * 0.5);
    std::vector<float> y(total);
    std::vector<float> in(block, 0.f);
    for(std::size_t n = 0; n + block <= total; n += block)
        filter.Process(in.data(), y.data() + n, block);
    std::vector<float> last(y.end() - long(tail), y.end());
    double peak = 0.; // sample peak: only for the start time, it misses the true peak at high pitch
    for(float v : last)
        peak = std::max(peak, double(std::fabs(v)));
    // Frequency from the rising zero crossings of the tail.
    double first = -1., latest = 0.;
    int crossings = 0;
    for(std::size_t i = 1; i < last.size(); ++i)
        if(last[i - 1] <= 0.f && last[i] > 0.f) {
            latest = double(i - 1) + double(-last[i - 1]) / double(last[i] - last[i - 1]);
            if(first < 0.)
                first = latest;
            ++crossings;
        }
    const double f = crossings > 1 ? kFs * double(crossings - 1) / (latest - first) : 0.;
    const double fundamental = Bin(last, f);
    double harmonics = 0.;
    for(int h = 2; h <= 7 && h * f < kFs / 2.; ++h) {
        const double m = Bin(last, h * f);
        harmonics += m * m;
    }
    std::size_t reached = total;
    const double limit = peak * 0.891;
    for(std::size_t i = 0; i < total; ++i)
        if(std::fabs(y[i]) >= limit) {
            reached = i;
            break;
        }
    return {20. * std::log10((f > 0. ? fundamental : peak) / double(kOscLevel) + 1e-12), f > 0. ? 1200. * std::log2(f / hz) : 0.,
            10. * std::log10(harmonics / (fundamental * fundamental + 1e-30) + 1e-20), double(reached) / kFs};
}

static double Distortion(double hz, Mode mode, float resonance, double amplitude) {
    Ladder filter;
    Parameters p;
    p.cutoff_note = float(NoteOf(hz));
    p.resonance = resonance;
    p.bass_comp = 1.f;
    p.mode = mode;
    filter.Set(p);
    const double tone = hz; // the worst place: at the cutoff, where the state swings most
    const std::size_t total = std::size_t(kFs), tail = std::size_t(kFs / 2);
    std::vector<float> in(total), out(total);
    for(std::size_t n = 0; n < total; ++n)
        in[n] = float(amplitude * std::sin(2. * kPi * tone / kFs * double(n)));
    for(std::size_t n = 0; n + 48 <= total; n += 48)
        filter.Process(in.data() + n, out.data() + n, 48);
    std::vector<float> last(out.end() - long(tail), out.end());
    const double fundamental = Bin(last, tone);
    double harmonics = 0.;
    for(int h = 2; h <= 7 && h * tone < kFs / 2.; ++h) {
        const double m = Bin(last, h * tone);
        harmonics += m * m;
    }
    return 10. * std::log10(harmonics / (fundamental * fundamental + 1e-30) + 1e-20);
}

int main() {
    static const double cutoffs[] = {30., 60., 125., 250., 500., 1000., 2000., 4000., 8000., 12000., 16000., 19000.};
    std::printf("Self-oscillation, resonance 100 %%, no input. Target peak %.2f.\n", double(kOscLevel));
    std::printf("Amplitude of the fundamental relative to the target in dB, by mode:\n%8s", "Hz");
    for(unsigned m = 0; m < kModeCount; ++m)
        std::printf("%8s", kNames[m]);
    std::printf("\n");
    double lo = 1e9, hi = -1e9;
    for(double hz : cutoffs) {
        std::printf("%8.0f", hz);
        for(unsigned m = 0; m < kModeCount; ++m) {
            const Osc o = Oscillate(hz, Mode(m));
            std::printf("%8.2f", o.peak_db);
            if(Mode(m) != Mode::Notch) {
                lo = std::min(lo, o.peak_db);
                hi = std::max(hi, o.peak_db);
            }
        }
        std::printf("\n");
    }
    std::printf("Range without Notch: %.2f .. %.2f dB\n\n", lo, hi);
    std::printf("LP24 oscillation detail:\n%8s %10s %10s %12s\n", "Hz", "cents", "THD dB", "start s");
    for(double hz : cutoffs) {
        const Osc o = Oscillate(hz, Mode::Lp24);
        std::printf("%8.0f %10.1f %10.1f %12.3f\n", hz, o.cents, o.thd_db, o.seconds);
    }
    std::printf("\nTHD in dB, sine at the cutoff, resonance 0 / 50 %%, bass comp 1:\n");
    std::printf("%8s %8s", "Hz", "level");
    for(unsigned m = 0; m < kModeCount; ++m)
        std::printf("%8s", kNames[m]);
    std::printf("\n");
    for(float resonance : {0.f, 0.5f})
        for(double hz : {200., 2000.})
            for(double amplitude : {0.25, 1.0}) {
                std::printf("%8.0f %8.2f", hz, amplitude);
                for(unsigned m = 0; m < kModeCount; ++m)
                    std::printf("%8.1f", Distortion(hz, Mode(m), resonance, amplitude));
                std::printf("   r=%.1f\n", double(resonance));
            }
    return 0;
}
