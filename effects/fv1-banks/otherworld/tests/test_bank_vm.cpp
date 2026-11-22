// SPDX-License-Identifier: MIT
// Consumer test: every Otherworld program runs on fv1-vm (float, 32 kHz) with a played phrase.
// Input: plucked notes (harmonics 1..6, 0.35 s decay) on L and R, a 16-step phrase with rests, and
// about -90 dBFS noise on both inputs like a real ADC (Moonquake draws its dice from the low input bits).
// Clocked programs get a 2 Hz square clock (0 / 0.8) plus that noise on R instead.
// Pots: the program's own @render values (upstream headers, kRender). Checks: finite output inside [-1, 1] and
// at least 6 of 8 one-second windows above -60 dBFS RMS (4 for Numbers Station (clocked), see below). With OR-ed SKP flags (fv1-vm 0.1.0) Mitosis
// stays silent and Avalanche falls silent after 5 s, so this also guards the component 0.2.0 fix.
#include "fv1bank/otherworld_programs.h"
#include "fv1bank/otherworld_version.h"
#include "fv1vm/fv1_vm.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
constexpr unsigned kRate = 32000, kBlock = 32, kSeconds = 8;

struct Result { bool finite = true, bounded = true; int loud_seconds = 0; double rms_db[kSeconds]{}; };

Result Run(unsigned index, seed::fx::fv1::Storage<float>& storage) {
    using namespace seed::fx::fv1;
    static Vm<float> vm;
    vm.Prepare({kRate, kBlock}, storage);
    vm.LoadProgram(fv1_bank_otherworld::kPrograms[index]);
    Parameters p;
    for(int i = 0; i < 3; ++i) p.pot[i] = fv1_bank_otherworld::kRender[index][i];
    vm.SetParameters(p);
    vm.Reset();
    const bool clocked = fv1_bank_otherworld::kClocked[index] != 0;
    const int phrase[16] = {57, 0, 60, 64, 67, 0, 0, 0, 69, 67, 64, 60, 0, 0, 55, 0};
    const double pi = std::acos(-1.0);
    double freq = 0, env = 0, ph = 0, acc = 0;
    uint32_t noise = 1;
    float l[kBlock], r[kBlock], ol[kBlock], orr[kBlock];
    Result res;
    for(unsigned t0 = 0; t0 < kRate * kSeconds; t0 += kBlock) {
        for(unsigned k = 0; k < kBlock; ++k) {
            const unsigned t = t0 + k;
            if(t % (kRate / 4) == 0) {
                const int note = phrase[(t / (kRate / 4)) % 16];
                if(note) { freq = 440.0 * std::pow(2.0, (note - 69) / 12.0); env = 0.5; ph = 0; }
            }
            double s = 0;
            for(int h = 1; h <= 6; ++h) s += std::sin(ph * h) / h;
            s *= env * 0.6;
            env *= std::exp(-1.0 / (0.35 * kRate));
            ph += 2 * pi * freq / kRate;
            noise = noise * 1664525u + 1013904223u;
            const float nz = (float(noise >> 8) / 16777216.f - 0.5f) * 6e-5f;
            l[k] = float(s) + nz;
            r[k] = (clocked ? ((t % (kRate / 2)) < (kRate / 4) ? 0.8f : 0.f) : float(s)) + nz;
        }
        vm.Process({l, r, ol, orr, kBlock});
        for(unsigned k = 0; k < kBlock; ++k) {
            const double a = ol[k], b = orr[k];
            if(!std::isfinite(a) || !std::isfinite(b)) res.finite = false;
            if(std::fabs(a) > 1.0 || std::fabs(b) > 1.0) res.bounded = false;
            acc += a * a + b * b;
            const unsigned t = t0 + k + 1;
            if(t % kRate == 0) {
                const double rms = std::sqrt(acc / (2.0 * kRate));
                const double db = rms > 0 ? 20 * std::log10(rms) : -999.0;
                res.rms_db[t / kRate - 1] = db;
                if(db > -60.0) ++res.loud_seconds;
                acc = 0;
            }
        }
    }
    return res;
}
} // namespace

int main() {
    auto storage = std::make_unique<seed::fx::fv1::Storage<float>>();
    int failures = 0;
    for(unsigned i = 0; i < fv1_bank_otherworld::kCount; ++i) {
        const Result r = Run(i, *storage);
        // Numbers Station (clocked) keys whole silent steps by design ("Rests"); host runs show one or two exactly
        // silent seconds in eight, so it gets a lower floor. Every other program needs six loud seconds.
        const int need = std::strcmp(fv1_bank_otherworld::kId[i], "179_numbers_station_clk") == 0 ? 4 : 6;
        const bool ok = r.finite && r.bounded && r.loud_seconds >= need;
        std::printf("%s %2u %-28s loud %d/%u, dB:", ok ? "PASS" : "FAIL", i + 1, fv1_bank_otherworld::kName[i],
                    r.loud_seconds, kSeconds);
        for(double db : r.rms_db) std::printf(" %.0f", db < -999 ? -999 : db);
        std::printf("%s%s\n", r.finite ? "" : " NON-FINITE", r.bounded ? "" : " OUT-OF-RANGE");
        failures += !ok;
    }
    if(failures) { std::printf("%d program(s) failed\n", failures); return 1; }
    std::printf("fv1-bank-otherworld %s on fv1-vm %s: all %u programs passed\n", SEED_FV1_BANK_OTHERWORLD_VERSION,
                SEED_FV1VM_VERSION, fv1_bank_otherworld::kCount);
    return 0;
}
