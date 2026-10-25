#pragma once
// Editable configuration: 8x8 grid plus source parameters (ARCHITECTURE.md § 3.3).
// The same serialization and CRC run in the firmware, the host tests and the editor.
#include "types.h"
#include <array>

namespace mod {
struct LfoParams {
    uint16_t rate = 6000;   // 0..kRateMax -> 0.02..20 Hz (Free)
    uint8_t shape = Sine;
    uint8_t sync = Free;
    uint8_t gate = 0;       // 0 = Gate 1, 1 = Gate 2 (GateClock)
    uint8_t division = kDefaultMidiDivision;
    uint8_t retrigger = 0;  // 1: restart on every Note On (Free only)
#if MOD_ON_DAISY_EXTENSIONS
    int8_t fm = 0;          // -100..+100: rate modulated by the other LFO, ±2 octaves (format 2, Free only)
#endif
};
struct RandomParams {
    uint16_t rate = 8000;
    uint8_t mode = Stepped;
    uint8_t sync = Free;
    uint8_t gate = 0;
    uint8_t division = kDefaultMidiDivision;
};

// Source parameter numbers of SET_SOURCE (PROTOCOL.md § 3.2).
enum SourceParam : uint8_t { ParamRate, ParamShape, ParamSync, ParamGate, ParamDivision, ParamRetrigger,
                             kSourceParamCount };
// LFO cross modulation (extension, IDENTIFY capability bit 6): value = fm + 100, 0..200.
constexpr uint8_t ParamFm = 6;
constexpr int kMaxFm = 100;

struct Patch {
    uint8_t format = 1;
    int8_t depth[kSources][kTargets]{}; // -100..+100
    uint8_t enabled[kSources]{};        // bit t: cell (s, t) switched on
    LfoParams lfo[2]{};
    RandomParams random{};
    char name[16]{};

    bool Enabled(std::size_t s, std::size_t t) const { return (enabled[s] >> t) & 1u; }
    void SetEnabled(std::size_t s, std::size_t t, bool on) {
        enabled[s] = uint8_t(on ? enabled[s] | (1u << t) : enabled[s] & ~(1u << t));
    }
    // Active = switched on and non-zero depth (an off cell keeps its depth).
    bool Active(std::size_t s, std::size_t t) const { return Enabled(s, t) && depth[s][t] != 0; }
    uint64_t ActiveMask() const {
        uint64_t mask = 0;
        for(std::size_t s = 0; s < kSources; ++s)
            for(std::size_t t = 0; t < kTargets; ++t)
                if(Active(s, t))
                    mask |= CellBit(s, t);
        return mask;
    }
    uint64_t EnabledMask() const {
        uint64_t mask = 0;
        for(std::size_t s = 0; s < kSources; ++s)
            mask |= uint64_t{enabled[s]} << (s * kTargets);
        return mask;
    }
    void SetEnabledMask(uint64_t mask) {
        for(std::size_t s = 0; s < kSources; ++s)
            enabled[s] = uint8_t(mask >> (s * kTargets));
    }
};

inline unsigned PopCount(uint64_t x) {
    unsigned n = 0;
    for(; x; x &= x - 1)
        ++n;
    return n;
}

enum class Status : uint8_t { Ok = 0, Range = 4, Limit = 7, NotAllowed = 8 };

inline Status ValidateSource(const LfoParams& p) {
    const bool ok = p.rate <= kRateMax && p.shape < kShapeCount && p.sync < kSyncCount && p.gate < 2
        && p.division < (p.sync == GateClock ? kGateDivisionCount : kMidiDivisionCount) && p.retrigger < 2
#if MOD_ON_DAISY_EXTENSIONS
        && p.fm >= -kMaxFm && p.fm <= kMaxFm
#endif
        ;
    return ok ? Status::Ok : Status::Range;
}
inline Status ValidateSource(const RandomParams& p) {
    const bool ok = p.rate <= kRateMax && p.mode < kRandomModeCount && p.sync < kSyncCount && p.gate < 2
        && p.division < (p.sync == GateClock ? kGateDivisionCount : kMidiDivisionCount);
    return ok ? Status::Ok : Status::Range;
}

// A patch is accepted or rejected as a whole, never partially.
inline Status Validate(const Patch& patch, uint64_t allowed) {
#if MOD_ON_DAISY_EXTENSIONS
    // Format 2 adds the LFO cross modulation; format 1 must not carry any.
    if(patch.format != 1 && patch.format != 2)
        return Status::Range;
    if(patch.format == 1 && (patch.lfo[0].fm != 0 || patch.lfo[1].fm != 0))
        return Status::Range;
#else
    if(patch.format != 1)
        return Status::Range;
#endif
    for(std::size_t s = 0; s < kSources; ++s)
        for(std::size_t t = 0; t < kTargets; ++t)
            if(patch.depth[s][t] < -kMaxDepth || patch.depth[s][t] > kMaxDepth)
                return Status::Range;
    for(const LfoParams& lfo : patch.lfo)
        if(ValidateSource(lfo) != Status::Ok)
            return Status::Range;
    if(ValidateSource(patch.random) != Status::Ok)
        return Status::Range;
    for(char c : patch.name)
        if(static_cast<unsigned char>(c) > 127)
            return Status::Range;
    const uint64_t active = patch.ActiveMask();
    if(active & ~allowed)
        return Status::NotAllowed;
    if(PopCount(active) > kMaxActive)
        return Status::Limit;
    return Status::Ok;
}

// CRC-32/ISO-HDLC (zlib): reflected 0xEDB88320, init and final XOR 0xFFFFFFFF.
// Nibble table: 64 bytes instead of 1 KiB; only the main loop calls it.
inline uint32_t Crc32Update(uint32_t crc, const uint8_t* data, std::size_t size) {
    constexpr uint32_t kTable[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    for(std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ kTable[crc & 15];
        crc = (crc >> 4) ^ kTable[crc & 15];
    }
    return crc;
}
inline uint32_t Crc32(const uint8_t* data, std::size_t size) {
    return ~Crc32Update(0xFFFFFFFFu, data, size);
}

// Fixed little-endian byte layout without padding (STORAGE.md § 3, PROTOCOL.md).
constexpr std::size_t kPatchBytes = 1 + kCells + kSources + 2 * 7 + 6 + 16; // 109
#if MOD_ON_DAISY_EXTENSIONS
constexpr std::size_t kPatchBytesV2 = kPatchBytes + 2; // format 2: + fm of LFO 1 and 2
using PatchBytes = std::array<uint8_t, kPatchBytesV2>;
inline std::size_t PatchSize(const Patch& p) { return p.format == 2 ? kPatchBytesV2 : kPatchBytes; }
#else
using PatchBytes = std::array<uint8_t, kPatchBytes>;
#endif

inline PatchBytes Serialize(const Patch& p) {
    PatchBytes b{};
    std::size_t i = 0;
    b[i++] = p.format;
    for(std::size_t s = 0; s < kSources; ++s)
        for(std::size_t t = 0; t < kTargets; ++t)
            b[i++] = static_cast<uint8_t>(p.depth[s][t]);
    for(uint8_t e : p.enabled)
        b[i++] = e;
    for(const LfoParams& l : p.lfo) {
        b[i++] = uint8_t(l.rate);
        b[i++] = uint8_t(l.rate >> 8);
        b[i++] = l.shape;
        b[i++] = l.sync;
        b[i++] = l.gate;
        b[i++] = l.division;
        b[i++] = l.retrigger;
    }
    b[i++] = uint8_t(p.random.rate);
    b[i++] = uint8_t(p.random.rate >> 8);
    b[i++] = p.random.mode;
    b[i++] = p.random.sync;
    b[i++] = p.random.gate;
    b[i++] = p.random.division;
    for(char c : p.name)
        b[i++] = static_cast<uint8_t>(c);
#if MOD_ON_DAISY_EXTENSIONS
    if(p.format == 2) {
        b[i++] = static_cast<uint8_t>(p.lfo[0].fm);
        b[i++] = static_cast<uint8_t>(p.lfo[1].fm);
    }
#endif
    return b;
}

inline Patch Deserialize(const PatchBytes& b) {
    Patch p;
    std::size_t i = 0;
    p.format = b[i++];
    for(std::size_t s = 0; s < kSources; ++s)
        for(std::size_t t = 0; t < kTargets; ++t)
            p.depth[s][t] = static_cast<int8_t>(b[i++]);
    for(uint8_t& e : p.enabled)
        e = b[i++];
    for(LfoParams& l : p.lfo) {
        l.rate = uint16_t(b[i] | (b[i + 1] << 8));
        i += 2;
        l.shape = b[i++];
        l.sync = b[i++];
        l.gate = b[i++];
        l.division = b[i++];
        l.retrigger = b[i++];
    }
    p.random.rate = uint16_t(b[i] | (b[i + 1] << 8));
    i += 2;
    p.random.mode = b[i++];
    p.random.sync = b[i++];
    p.random.gate = b[i++];
    p.random.division = b[i++];
    for(char& c : p.name)
        c = static_cast<char>(b[i++]);
#if MOD_ON_DAISY_EXTENSIONS
    if(p.format == 2) {
        p.lfo[0].fm = static_cast<int8_t>(b[i++]);
        p.lfo[1].fm = static_cast<int8_t>(b[i++]);
    }
#endif
    return p;
}

inline uint32_t PatchCrc(const Patch& p) {
    const PatchBytes bytes = Serialize(p);
#if MOD_ON_DAISY_EXTENSIONS
    return Crc32(bytes.data(), PatchSize(p));
#else
    return Crc32(bytes.data(), bytes.size());
#endif
}

// CRC-32 over key and all column descriptions: changes whenever a column's meaning
// could have changed, so stored patches and editor caches are never misapplied.
inline uint32_t SchemaHash(const InstrumentInfo& info) {
    uint32_t crc = 0xFFFFFFFFu;
    auto text = [&](const char* s) {
        for(; s && *s; ++s)
            crc = Crc32Update(crc, reinterpret_cast<const uint8_t*>(s), 1);
        const uint8_t end = 0;
        crc = Crc32Update(crc, &end, 1);
    };
    text(info.key);
    for(const TargetInfo& t : info.targets) {
        text(t.name);
        text(t.span_text);
        const uint32_t span = uint32_t(t.span * 1000000.0f + 0.5f);
        const uint8_t bytes[6] = {uint8_t(span), uint8_t(span >> 8), uint8_t(span >> 16), uint8_t(span >> 24),
                                  uint8_t(t.per_voice), uint8_t(t.latched)};
        crc = Crc32Update(crc, bytes, sizeof(bytes));
    }
    const uint8_t tail[2] = {info.unavailable_sources, 1 /* Poly AT global: last note */};
    crc = Crc32Update(crc, tail, sizeof(tail));
    return ~crc;
}
}
