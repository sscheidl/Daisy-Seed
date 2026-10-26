// MIT. Host timing of the Tape FX stages: relative costs on the build machine only.
// This is not a Seed3 (Cortex-M7) CPU measurement; see docs/REPORT.md.
#include "../src/tape_fx.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace tape = seed::fx::tape;
constexpr unsigned kBlock = 48, kSeconds = 60;

static double Run(const char* name, tape::Parameters p, bool reference, bool glide = false, float level = .5f) {
    seed::fx::TapeFx fx;
    auto storage = std::make_unique<seed::fx::TapeFx::Storage>();
    fx.SetParameters(p);
    fx.SetReferenceMode(reference);
    fx.Prepare({48000, kBlock}, *storage);
    std::vector<float> l(kBlock), r(kBlock), ol(kBlock), orr(kBlock);
    uint32_t seed = 1;
    double sink = 0;
    const auto start = std::chrono::steady_clock::now();
    for(unsigned b = 0; b < kSeconds * 1000; ++b) {
        if(glide && b % 50 == 0) { // a pot sweep that never settles
            p.age = (b / 50) % 2 ? .2f : .8f;
            p.drive = (b / 50) % 2 ? .3f : .7f;
            fx.SetParameters(p);
        }
        for(unsigned i = 0; i < kBlock; ++i) {
            seed = seed * 1664525u + 1013904223u;
            l[i] = level * (float(seed >> 8) / 8388608.f - 1.f);
            r[i] = -l[i];
        }
        fx.Process({l.data(), r.data(), ol.data(), orr.data(), kBlock});
        sink += ol[0];
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
                      (double(kSeconds) * 48000);
    std::printf("%-46s %7.1f ns/frame  (%5.2f %% of real time)  [%g]\n", name, ns, ns / 20833.3 * 100, sink * 0);
    return ns;
}

int main() {
    std::puts("Tape FX host benchmark (stereo frames, 48-frame blocks, 60 s each; host CPU, not Seed3)");
    tape::Parameters p; // TapeVibe init values
    Run("reference: TapeVibe init, libm sinf", p, true);
    Run("optimized: TapeVibe init, polynomial sine", p, false);
    tape::Parameters q = p;
    q.hiss = 0; q.dropout = 0;
    Run("optimized: hiss and dropout off", q, false);
    q = p; q.wow = q.flutter = 0;
    Run("optimized: wow/flutter depth 0 (still computed)", q, false);
    Run("optimized: continuous glide (Age, Drive)", p, false, true);
    q = p; q.curve = tape::DriveCurve::MixTape;
    Run("optimized: + MixForge TAPE curve", q, false);
    q = p; q.curve = tape::DriveCurve::Fold;
    Run("optimized: + MixForge FOLD curve", q, false);
    q = p; q.compressor = true;
    Run("optimized: + compressor, signal above threshold", q, false);
    q.threshold_db = 0;
    Run("optimized: + compressor, signal below knee", q, false, false, .05f);
    q = p; q.compressor = true;
    Run("reference: + compressor above threshold", q, true);
    q = p; q.bitcrush = true; q.bits = 8; q.crush_stride = 3;
    Run("optimized: + bitcrusher", q, false);
    q = p; q.compressor = q.bitcrush = true; q.curve = tape::DriveCurve::MixTape;
    Run("optimized: all MixForge stages", q, false);
}
