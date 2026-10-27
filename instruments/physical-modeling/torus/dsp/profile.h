#pragma once
#include <array>
#include <cstdint>
#include <limits>
#if PM_PROFILE && defined(__arm__)
#include "stm32h7xx.h"
#endif

namespace torus::profile {
enum Stage : unsigned { Voices, Filters, Reverb, Output, Hash, ClockHz, StageCount };
struct Snapshot {
    uint64_t total = 0;
    uint32_t calls = 0, maximum = 0, blocks = 0;
    unsigned stage = 0;
};
#if PM_PROFILE
struct Counter { uint64_t total = 0; uint32_t calls = 0, maximum = 0; };
inline std::array<Counter, 4> counters{};
inline uint32_t blocks = 0, clock_hz = 0;
inline uint64_t render_hash = 0;
inline uint32_t corpus_version = 0;
#if defined(__arm__)
inline uint32_t Read() { return DWT->CYCCNT; }
inline void Init(uint32_t hz) {
    clock_hz = hz;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
#else
// Host tests inject a synthetic clock; never interpreted as hardware cycles.
inline uint32_t ticks = 0;
inline uint32_t Read() { return ticks; }
inline void Init(uint32_t hz) { clock_hz = hz; }
#endif
inline void Reset() { counters = {}; blocks = 0; }
inline void EndBlock() { if(blocks != UINT32_MAX) ++blocks; }
inline Snapshot Get(unsigned stage) {
    if(stage == Hash) return {render_hash, corpus_version, 0, blocks, stage};
    if(stage == ClockHz) return {clock_hz, 0, 0, blocks, stage};
    const auto& c = counters[stage];
    return {c.total, c.calls, c.maximum, blocks, stage};
}
class Scope {
  public:
    explicit Scope(Stage stage) : stage_(stage), start_(Read()) {}
    ~Scope() {
        const uint32_t elapsed = Read() - start_;
        auto& c = counters[stage_];
        if(c.calls != UINT32_MAX) ++c.calls;
        if(UINT64_MAX - c.total >= elapsed) c.total += elapsed; else c.total = UINT64_MAX;
        if(elapsed > c.maximum) c.maximum = elapsed;
    }
  private:
    Stage stage_;
    uint32_t start_;
};
#else
class Scope { public: explicit Scope(Stage) {} };
#endif
// Independent diagnostic CC replies; the existing CPU frame and Mod on Daisy
// SysEx 0x7d protocol remain unchanged. Serialization is main-loop-only.
inline bool Request(unsigned channel, unsigned cc, unsigned value) {
    return channel == 15 && cc == 119 && value >= 16 && value <= 22;
}
inline std::array<uint8_t, 87> Encode(const Snapshot& s) {
    std::array<uint8_t, 87> packet{};
    for(unsigned i = 0; i < 29; ++i) {
        packet[3*i] = 0xbf; packet[3*i+1] = uint8_t(70+i);
    }
    auto put = [&](unsigned cc, uint64_t value, unsigned digits) {
        for(unsigned i = 0; i < digits; ++i)
            packet[3*(cc-70+i)+2] = uint8_t((value >> (7*i)) & 127u);
    };
    put(70, 1, 1); put(71, s.stage, 1); put(72, s.calls, 5);
    put(77, s.total, 10); put(87, s.maximum, 5);
    put(92, s.blocks, 5); put(98, 64, 1);
    return packet;
}
}
