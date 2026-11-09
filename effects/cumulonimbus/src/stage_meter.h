#pragma once
// SPDX-License-Identifier: MIT
// Profiling meter for the Cumulonimbus diagnostic images: per-stage cycle maxima/sums, a bounded
// histogram of whole-callback cycles, callback starts and missed DMA periods (from the interval
// between callback starts, so an overlong callback cannot hide lost periods), and a telemetry
// record format for the USB-MIDI probe (tools/cumulonimbus_probe.py).
//
// Threads: Begin/Add/End and Service run in the audio callback only. The main loop requests a
// reset or a snapshot through atomics and reads the snapshot after `ready` (acquire). Nothing
// here allocates, locks or blocks.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#define CUMULONIMBUS_METER_VERSION "0.1.0"

namespace cumulonimbus::diag {

enum Stage : unsigned { kPiano, kGrainWrite, kGrainRender, kConvert, kReverb, kTotal, kStageCount };
constexpr unsigned kBins = 128; // 1 % of the block budget each; the last bin holds >= 127 %

struct MeterSnapshot {
    uint32_t blocks = 0, missed = 0, overruns = 0, budget = 1;
    uint32_t max[kStageCount] = {};
    uint64_t sum[kStageCount] = {};
    uint32_t histogram[kBins] = {};
};

class StageMeter {
  public:
    void Init(uint32_t budget_cycles) {
        budget_ = budget_cycles ? budget_cycles : 1;
        Clear();
    }
    // Callback start, before any work. `now` is the cycle counter.
    void Begin(uint32_t now) {
        if(reset_.exchange(false, std::memory_order_acq_rel))
            Clear();
        if(started_) {
            const uint32_t interval = now - last_start_;
            // A period counts as missed when a start arrives later than 1.5 budgets.
            if(interval > budget_ + budget_ / 2) {
                const uint32_t lost = (interval + budget_ / 2) / budget_ - 1;
                missed_ = missed_ > UINT32_MAX - lost ? UINT32_MAX : missed_ + lost;
            }
        }
        started_ = true;
        last_start_ = now;
        for(auto& c : current_)
            c = 0;
    }
    void Add(Stage stage, uint32_t cycles) { current_[stage] += cycles; }
    // Callback end. `total` is the whole-callback cycle count.
    void End(uint32_t total) {
        current_[kTotal] = total;
        for(unsigned s = 0; s < kStageCount; ++s) {
            if(current_[s] > max_[s]) max_[s] = current_[s];
            sum_[s] += current_[s];
        }
        uint64_t bin = uint64_t(total) * 100u / budget_;
        if(bin >= kBins) bin = kBins - 1;
        ++histogram_[bin];
        if(blocks_ != UINT32_MAX) ++blocks_;
        if(total > budget_ && overruns_ != UINT32_MAX) ++overruns_;
        // Snapshot hand-off, audio side: copy only while the main loop is not reading.
        if(request_.load(std::memory_order_relaxed) && !ready_.load(std::memory_order_acquire)) {
            snapshot_.blocks = blocks_;
            snapshot_.missed = missed_;
            snapshot_.overruns = overruns_;
            snapshot_.budget = budget_;
            for(unsigned s = 0; s < kStageCount; ++s) {
                snapshot_.max[s] = max_[s];
                snapshot_.sum[s] = sum_[s];
            }
            for(unsigned b = 0; b < kBins; ++b)
                snapshot_.histogram[b] = histogram_[b];
            request_.store(false, std::memory_order_relaxed);
            ready_.store(true, std::memory_order_release);
        }
    }

    // Main loop.
    void RequestReset() { reset_.store(true, std::memory_order_release); }
    void RequestSnapshot() { request_.store(true, std::memory_order_relaxed); }
    bool SnapshotReady() const { return ready_.load(std::memory_order_acquire); }
    const MeterSnapshot& Snapshot() const { return snapshot_; }
    void ReleaseSnapshot() { ready_.store(false, std::memory_order_release); }
    uint32_t Budget() const { return budget_; }

  private:
    void Clear() {
        blocks_ = missed_ = overruns_ = 0;
        started_ = false;
        for(unsigned s = 0; s < kStageCount; ++s) {
            max_[s] = 0;
            sum_[s] = 0;
        }
        for(auto& h : histogram_)
            h = 0;
    }
    static_assert(std::atomic<bool>::is_always_lock_free, "meter flags must be lock-free");
    uint32_t budget_ = 1, last_start_ = 0, blocks_ = 0, missed_ = 0, overruns_ = 0;
    bool started_ = false;
    uint32_t current_[kStageCount] = {}, max_[kStageCount] = {};
    uint64_t sum_[kStageCount] = {};
    uint32_t histogram_[kBins] = {};
    MeterSnapshot snapshot_{};
    std::atomic<bool> reset_{false}, request_{false}, ready_{false};
};

// Percentile in 1/10000 of the budget, from the 1 % histogram (upper bin edge, conservative).
inline uint32_t Percentile(const MeterSnapshot& s, unsigned per_mille) {
    uint64_t total = 0;
    for(auto h : s.histogram)
        total += h;
    if(!total)
        return 0;
    const uint64_t rank = (total * per_mille + 999) / 1000; // 1-based
    uint64_t seen = 0;
    for(unsigned b = 0; b < kBins; ++b) {
        seen += s.histogram[b];
        if(seen >= rank)
            return (b + 1) * 100u;
    }
    return kBins * 100u;
}

// ---- Telemetry records ---------------------------------------------------------------------------
// One snapshot is kMetricCount u32 values sent as records of three on MIDI channel 16 (0xBF):
// CC100 = 3 (protocol id), CC101 = record index, CC102..116 = three values as five 7-bit digits
// each (little endian), CC117 = kMetricCount, CC118 = checksum (sum of CC101..CC117 data & 127),
// CC119 = 64. Request: channel 16 CC119 = 1 (snapshot) or 2 (reset). Scenario CC20, grain cap
// CC21, stimulus CC22 (diagnostic images only).
enum Metric : unsigned {
    kScenario, kCap, kStimulus,
    kInternalRate, kBlockFrames, kBudgetCycles,
    kBlocks, kWindowMs, kMissedPeriods,
    kOverruns, kTotalMax, kTotalMean,
    kP50, kP90, kP99,
    kPianoMax, kWriteMax, kRenderMax,
    kConvertMax, kReverbMax, kRenderMean,
    kGrainsActive, kGrainsPeak, kRejected,
    kStartedHigh, kStartedMedium, kMidiOverruns,
    kVoicesActive, kVoicesPeak, kFlags,
    kPianoMean, kReverbMean, kWriteMean,
    kMetricCount
};
static_assert(kMetricCount % 3 == 0, "records carry three metrics");
constexpr unsigned kRecords = kMetricCount / 3;
constexpr uint8_t kProtocolId = 3;
enum Flag : uint32_t {
    kFlagProfiling = 1, kFlagBypass = 2, kFlagFreeze = 4, kFlagSuspended = 8, kFlagFxPage = 16,
    kFlagRate32 = 32, kFlagMono = 64
};
using Metrics = std::array<uint32_t, kMetricCount>;

inline void FillMeterMetrics(const MeterSnapshot& s, Metrics& m) {
    const uint32_t n = s.blocks ? s.blocks : 1;
    m[kBlocks] = s.blocks;
    m[kMissedPeriods] = s.missed;
    m[kOverruns] = s.overruns;
    m[kBudgetCycles] = s.budget;
    m[kTotalMax] = s.max[kTotal];
    m[kTotalMean] = uint32_t(s.sum[kTotal] / n);
    m[kP50] = Percentile(s, 500);
    m[kP90] = Percentile(s, 900);
    m[kP99] = Percentile(s, 990);
    m[kPianoMax] = s.max[kPiano];
    m[kWriteMax] = s.max[kGrainWrite];
    m[kRenderMax] = s.max[kGrainRender];
    m[kConvertMax] = s.max[kConvert];
    m[kReverbMax] = s.max[kReverb];
    m[kRenderMean] = uint32_t(s.sum[kGrainRender] / n);
    m[kPianoMean] = uint32_t(s.sum[kPiano] / n);
    m[kReverbMean] = uint32_t(s.sum[kReverb] / n);
    m[kWriteMean] = uint32_t(s.sum[kGrainWrite] / n);
}

inline std::array<uint8_t, 60> EncodeRecord(const Metrics& m, unsigned record) {
    std::array<uint8_t, 60> p{};
    for(unsigned i = 0; i < 20; ++i) {
        p[3 * i] = 0xBF;
        p[3 * i + 1] = uint8_t(100 + i);
    }
    auto data = [&](unsigned cc) -> uint8_t& { return p[3 * (cc - 100) + 2]; };
    data(100) = kProtocolId;
    data(101) = uint8_t(record & 127u);
    for(unsigned v = 0; v < 3; ++v) {
        const uint32_t value = record < kRecords ? m[record * 3 + v] : 0u;
        for(unsigned d = 0; d < 5; ++d)
            data(102 + v * 5 + d) = uint8_t((uint64_t(value) >> (7 * d)) & 127u);
    }
    data(117) = uint8_t(kMetricCount);
    unsigned sum = 0;
    for(unsigned cc = 101; cc <= 117; ++cc)
        sum += data(cc);
    data(118) = uint8_t(sum & 127u);
    data(119) = 64;
    return p;
}

// Channel-16 control CCs of the diagnostic images.
struct ControlCc {
    static constexpr uint8_t kScenario = 20, kCap = 21, kStimulus = 22, kTelemetry = 119;
};

} // namespace cumulonimbus::diag
