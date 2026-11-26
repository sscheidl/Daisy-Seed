#pragma once
#include "util/CpuLoadMeter.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
namespace physical_modeling {
struct CpuSnapshot {
    float average = 0, maximum = 0;
    uint32_t blocks = 0, overruns = 0;
    uint8_t model = 0;
};
// Audio IRQ owns the meter/counters. Transfer snapshots through SPSC, never
// read meter floats concurrently from the main loop.
class CpuTelemetry {
  public:
    void Init(float sample_rate, unsigned block_size) {
        meter_.Init(sample_rate, int(block_size));
        ticks_per_block_ = float(daisy::System::GetTickFreq()) * block_size / sample_rate;
        blocks_ = overruns_ = 0;
    }
    void Begin(bool reset) {
        if(reset) { meter_.Reset(); blocks_ = overruns_ = 0; }
        start_ = daisy::System::GetTick();
        meter_.OnBlockStart();
    }
    void End() {
        const uint32_t elapsed = daisy::System::GetTick() - start_;
        meter_.OnBlockEnd();
        if(blocks_ != UINT32_MAX) ++blocks_;
        if(float(elapsed) > ticks_per_block_ && overruns_ != UINT32_MAX) ++overruns_;
    }
    CpuSnapshot Snapshot(unsigned model) const {
        return {meter_.GetAvgCpuLoad(), meter_.GetMaxCpuLoad(), blocks_, overruns_, uint8_t(model)};
    }
  private:
    daisy::CpuLoadMeter meter_;
    float ticks_per_block_ = 0;
    uint32_t start_ = 0, blocks_ = 0, overruns_ = 0;
};
inline bool CpuRequest(unsigned channel, unsigned cc, unsigned value) {
    return channel == 15 && cc == 119 && (value == 1 || value == 2);
}
// Channel 16 CC100..119, 20 ordinary three-byte MIDI messages. Using channel
// messages also avoids depending on this pinned USB transport's SysEx framing.
inline std::array<uint8_t, 60> EncodeCpuSnapshot(const CpuSnapshot& snapshot) {
    std::array<uint8_t, 60> packet{};
    for(unsigned i = 0; i < 20; ++i) { packet[3*i] = 0xbf; packet[3*i+1] = uint8_t(100+i); }
    auto value = [&](unsigned cc, uint32_t number, unsigned digits) {
        for(unsigned i = 0; i < digits; ++i) packet[3*(cc-100+i)+2] = uint8_t((number >> (7*i)) & 127);
    };
    auto load = [](float number) {
        if(!std::isfinite(number) || number < 0) return uint32_t{0};
        return uint32_t(std::min(2097151.0f, number * 10000.0f) + 0.5f);
    };
    value(100, 1, 1); // Protocol version; starts a new snapshot frame.
    value(101, load(snapshot.average), 3); value(104, load(snapshot.maximum), 3);
    value(107, snapshot.blocks, 5); value(112, snapshot.overruns, 5);
    value(117, snapshot.model, 1); value(118, 4, 1); value(119, 64, 1); // End, never a request.
    return packet;
}
}
