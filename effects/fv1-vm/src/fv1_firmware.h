#pragma once
// SPDX-License-Identifier: MIT
// Board-independent parts of the fv1-vm FX firmware: own demo programs, program bank, test
// signal, remote control over MIDI CC and the CPU telemetry packet. Host-tested in
// tests/test_firmware.cpp; src/main.cpp binds them to the Seed3 Desktop Kit.
#include "fv1vm/fv1_asm.h"
#include "fv1vm/fv1_vm.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fv1_fw {
namespace fv1 = seed::fx::fv1;

// ---- Own demo programs (MIT, written for this firmware) -------------------------------------------
// 0 "thru": DACL = ADCL, DACR = ADCR.
// 1 "echo": mono echo of 16383 samples (0.5 s at 32 768 Hz); POT1 feedback, POT2 wet level.
inline constexpr unsigned kBuiltinCount = 2;
inline void BuildBuiltin(unsigned index, uint32_t (&words)[fv1::kProgramWords]) {
    using namespace fv1::assemble;
    for(auto& w : words) w = Nop();
    unsigned n = 0;
    auto put = [&](uint32_t w) { words[n++] = w; };
    if(index == 0) {
        put(Rdax(ADCL, 1.0)); put(Wrax(DACL, 0.0));
        put(Rdax(ADCR, 1.0)); put(Wrax(DACR, 0.0));
        return;
    }
    put(Rda(16383, 1.0));   // delayed sample
    put(Wrax(REG0, 1.0));   // keep it in REG0
    put(Mulx(POT1));        // feedback
    put(Rdax(ADCL, 0.5));
    put(Rdax(ADCR, 0.5));   // + mono input
    put(Wra(0, 0.0));       // write the line head
    put(Rdax(REG0, 1.0));
    put(Mulx(POT2));        // wet level
    put(Wrax(REG0 + 1, 1.0));
    put(Rdax(ADCL, 1.0)); put(Wrax(DACL, 0.0));
    put(Rdax(REG0 + 1, 1.0));
    put(Rdax(ADCR, 1.0)); put(Wrax(DACR, 0.0));
}

// ---- Program bank: own demos, or a local generated table (make PROGRAMS=local) ---------------------
struct ProgramTable {
    const uint32_t (*words)[fv1::kProgramWords] = nullptr;
    unsigned count = 0;
};
class ProgramBank {
  public:
    // A table with count 0 selects the built-in demo programs.
    void Init(ProgramTable local) {
        local_ = local;
        for(unsigned i = 0; i < kBuiltinCount; ++i) BuildBuiltin(i, builtin_[i]);
    }
    bool Local() const { return local_.count > 0; }
    unsigned Count() const { return Local() ? local_.count : kBuiltinCount; }
    const uint32_t* Words(unsigned index) const {
        index %= Count();
        return Local() ? local_.words[index] : builtin_[index];
    }
  private:
    ProgramTable local_{};
    uint32_t builtin_[kBuiltinCount][fv1::kProgramWords]{};
};

// Index steps with wrap-around (keys: -1, +1, -10, +10).
inline unsigned StepIndex(unsigned index, int delta, unsigned count) {
    if(!count) return 0;
    const long long m = (static_cast<long long>(index) + delta) % static_cast<long long>(count);
    return unsigned(m < 0 ? m + count : m);
}

// ---- Test signal: the LCG noise of the host runner, amplitude 0.3 ---------------------------------
class Noise {
  public:
    explicit Noise(uint32_t seed = 12345) : x_(seed) {}
    float Next() {
        x_ = (1103515245u * x_ + 12345u) & 0x7FFFFFFFu;
        return (float(x_) / 2147483648.f - .5f) * 2.f * .3f;
    }
  private:
    uint32_t x_;
};

// ---- Remote control: channel 16 (index 15) control changes, plus standard Program Change ------------
// CC22 program index high 7 bits (latched), CC23 low 7 bits (applies, 0-based),
// CC24 input (0 line, 1 internal noise), CC25..27 POT0..2 = value/127 (remote pots until a
// panel pot moves), CC28 bypass (0 off, 1 on), CC119 telemetry (1 read, 2 reset and read).
enum class Command : uint8_t { None, Program, Input, Pot, Bypass, Telemetry };
struct Action {
    Command command = Command::None;
    unsigned value = 0, index = 0;
};
// Standard MIDI on any channel: Bank Select MSB (CC0) or LSB (CC32) chooses a bank of 128 programs
// (MSB if non-zero, else LSB); Program Change n then selects index bank * 128 + n. Like CC22/CC23, an
// index beyond the program count wraps around.
class Remote {
  public:
    Action Handle(unsigned channel, unsigned cc, unsigned value) {
        if(channel > 15 || value > 127) return {};
        if(cc == 0 || cc == 32) {
            (cc == 0 ? bank_msb_ : bank_lsb_) = value;
            return {};
        }
        if(channel != 15) return {};
        switch(cc) {
            case 22: high_ = value; return {};
            case 23: return {Command::Program, (high_ << 7) | value, 0};
            case 24: return value <= 1 ? Action{Command::Input, value, 0} : Action{};
            case 25: case 26: case 27: return {Command::Pot, value, cc - 25};
            case 28: return value <= 1 ? Action{Command::Bypass, value, 0} : Action{};
            case 119: return value == 1 || value == 2 ? Action{Command::Telemetry, value, 0} : Action{};
            default: return {};
        }
    }
    Action HandleProgramChange(unsigned channel, unsigned program) {
        if(channel > 15 || program > 127) return {};
        const unsigned bank = bank_msb_ ? bank_msb_ : bank_lsb_;
        return {Command::Program, bank * 128 + program, 0};
    }
  private:
    unsigned high_ = 0, bank_msb_ = 0, bank_lsb_ = 0;
};

// ---- CPU telemetry ------------------------------------------------------------------------------------
// Cycles per audio block against the block budget (DWT->CYCCNT on the board).
struct Snapshot {
    uint64_t sum_cycles = 0;
    uint32_t max_cycles = 0, blocks = 0, overruns = 0, budget = 1;
    uint16_t program = 0;
    uint8_t flags = 0;
};
enum Flag : uint8_t { kDouble = 1, kFastMemory = 2, kBypass = 4, kNoise = 8, kLocalPrograms = 16 };
class Meter {
  public:
    void Init(uint32_t budget_cycles) { budget_ = budget_cycles ? budget_cycles : 1; Reset(); }
    void Reset() { sum_ = 0; max_ = blocks_ = overruns_ = 0; }
    void Add(uint32_t cycles) {
        sum_ += cycles;
        if(cycles > max_) max_ = cycles;
        if(blocks_ != UINT32_MAX) ++blocks_;
        if(cycles > budget_ && overruns_ != UINT32_MAX) ++overruns_;
    }
    Snapshot Take(uint16_t program, uint8_t flags) const {
        return {sum_, max_, blocks_, overruns_, budget_, program, flags};
    }
  private:
    uint64_t sum_ = 0;
    uint32_t max_ = 0, blocks_ = 0, overruns_ = 0, budget_ = 1;
};

// Twenty channel-16 CCs, CC100..119 (the CC-packet pattern of instruments/physical-modeling,
// own protocol id 2 so its decoder ignores this packet):
// CC100 = 2, CC101-103 average load, CC104-106 peak load (both in 1/10000 of the block budget),
// CC107-111 blocks, CC112-115 overruns, CC116-117 program index (0-based), CC118 flags, CC119 = 64.
// Multi-digit values are little-endian 7-bit groups.
inline std::array<uint8_t, 60> Encode(const Snapshot& s) {
    std::array<uint8_t, 60> p{};
    for(unsigned i = 0; i < 20; ++i) { p[3 * i] = 0xBF; p[3 * i + 1] = uint8_t(100 + i); }
    auto put = [&](unsigned cc, uint64_t number, unsigned digits) {
        const uint64_t limit = (uint64_t(1) << (7 * digits)) - 1;
        if(number > limit) number = limit;
        for(unsigned i = 0; i < digits; ++i) p[3 * (cc - 100 + i) + 2] = uint8_t((number >> (7 * i)) & 127u);
    };
    const uint64_t budget = s.budget ? s.budget : 1;
    const uint64_t avg = s.blocks ? (s.sum_cycles * 10000u + budget * s.blocks / 2) / (budget * s.blocks) : 0;
    const uint64_t peak = (uint64_t(s.max_cycles) * 10000u + budget / 2) / budget;
    put(100, 2, 1);
    put(101, avg, 3);
    put(104, peak, 3);
    put(107, s.blocks, 5);
    put(112, s.overruns, 4);
    put(116, s.program, 2);
    put(118, s.flags & 127u, 1);
    put(119, 64, 1);
    return p;
}

} // namespace fv1_fw
