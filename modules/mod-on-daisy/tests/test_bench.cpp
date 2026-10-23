// Every bench scenario patch is valid for the bench instrument (TEST_PLAN_PHASE2.md § 3.2).
#include "../bench/src/scenarios.h"
#include <cassert>
#include <cstdio>

int main() {
    const mod::InstrumentInfo info = bench::MakeInfo();
    const uint64_t allowed = mod::AllowedCells(info);
    assert(allowed == ~uint64_t{0}); // the bench instrument offers all 64 cells
    const unsigned expected_active[bench::kScenarioCount] = {0, 0, 1, 4, 8, 8, 64, 8, 8};
    for(uint8_t n = 0; n < bench::kScenarioCount; ++n) {
        bench::Scenario s;
        const bool available = bench::MakeScenario(n, s);
        if(n == 6 && mod::kMaxActive < mod::kCells) {
            assert(!available);
            continue;
        }
        assert(available);
        assert(mod::Validate(s.patch, allowed) == mod::Status::Ok);
        assert(mod::PopCount(s.patch.ActiveMask()) == expected_active[n]);
        assert(s.voices == (n == 5 || n == 6 ? bench::kMaxVoices : 4u));
    }
    bench::Scenario s;
    assert(!bench::MakeScenario(bench::kScenarioCount, s));
    // S4 uses every source exactly once, Poly AT on a per-voice target.
    bench::MakeScenario(4, s);
    for(std::size_t src = 0; src < mod::kSources; ++src)
        assert(s.patch.enabled[src] && !(s.patch.enabled[src] & (s.patch.enabled[src] - 1)));
    assert(s.patch.Active(mod::PolyAt, bench::Level) && info.targets[bench::Level].per_voice);
    std::printf("Bench scenarios valid (kMaxActive %u).\n", unsigned(mod::kMaxActive));
}
