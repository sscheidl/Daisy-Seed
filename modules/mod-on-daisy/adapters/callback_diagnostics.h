#pragma once
// MIT. Optional libDaisy callback meter, read through the existing GET_STATS API.
#include "mod_on_daisy/controller.h"
#include "util/CpuLoadMeter.h"
#include <atomic>
#include <cmath>

#define MOD_CALLBACK_DIAGNOSTICS_VERSION "0.1.0"
namespace mod::adapter {
class CallbackDiagnostics : public Diagnostics {
  public:
    void Init(float sample_rate, unsigned block_size) {
        meter_.Init(sample_rate, int(block_size));
        budget_ticks_ = float(daisy::System::GetTickFreq()) * block_size / sample_rate;
        blocks_ = overruns_ = 0;
    }
    // Audio only. Main requests a reset; no main-thread access to meter floats.
    void Begin() {
        if(reset_.exchange(false, std::memory_order_relaxed)) {
            meter_.Reset();
            blocks_ = overruns_ = 0;
        }
        start_ = daisy::System::GetTick();
        meter_.OnBlockStart();
    }
    void End() {
        const uint32_t elapsed = daisy::System::GetTick() - start_;
        meter_.OnBlockEnd();
        if(blocks_ != UINT32_MAX) ++blocks_;
        if(float(elapsed) > budget_ticks_ && overruns_ != UINT32_MAX) ++overruns_;
        Stats s{};
        s.ticks = blocks_;
        s.overruns = overruns_;
        s.load_avg = Encode(meter_.GetAvgCpuLoad());
        s.load_max = Encode(meter_.GetMaxCpuLoad());
        snapshots_.Back() = s;
        snapshots_.Publish();
    }
    Stats GetStats() override {
        snapshots_.Fetch();
        return snapshots_.Front();
    }
    void ResetStats() override { reset_.store(true, std::memory_order_relaxed); }
    bool SetScenario(uint8_t) override { return false; }
  private:
    static uint32_t Encode(float load) {
        if(!std::isfinite(load) || load <= 0) return 0;
        return uint32_t(std::min(4294967040.f, load * 10000.f));
    }
    static_assert(std::atomic<bool>::is_always_lock_free);
    daisy::CpuLoadMeter meter_;
    TripleBuffer<Stats> snapshots_;
    std::atomic<bool> reset_{false};
    float budget_ticks_ = 0;
    uint32_t start_ = 0, blocks_ = 0, overruns_ = 0;
};
} // namespace mod::adapter
