#pragma once
// SPDX-License-Identifier: MIT
// SQ SysEx codec (sequencers/step-seq/docs/PROTOCOL.md), shared by the firmware and the
// editor. Mirrors tools/sq_codec.py; contracts/sq_vectors.json checks both.
#include <cstddef>
#include <cstdint>

namespace sq {

constexpr uint8_t kHeader[4] = {0xF0, 0x7D, 0x53, 0x51};
constexpr uint8_t kVersion = 1, kVersionMinor = 0;
constexpr std::size_t kMaxFrame = 120, kOverhead = 11, kMaxPayload = kMaxFrame - kOverhead;
constexpr uint8_t kResponse = 0x40, kMonitorEvent = 0x7E, kNak = 0x7F;

enum Type : uint8_t {
    Identify = 0x01, GetComponent = 0x02, GetParamDesc = 0x03, GetEnumLabel = 0x04,
    GetParams = 0x05, SetParam = 0x06,
    GetPattern = 0x10, SetPattern = 0x11, SetStep = 0x12,
    TransportCmd = 0x20, GetState = 0x21,
    GetCal = 0x28, SetCal = 0x29, CvEnable = 0x2A,
    Monitor = 0x30, GetStats = 0x31, ResetStats = 0x32,
};
enum Error : uint8_t {
    ErrLength = 1, ErrCrc = 2, ErrUnknownType = 3, ErrBadArgument = 4, ErrBusy = 5,
    ErrVersion = 6, ErrUidMismatch = 7, ErrNotCalibrated = 8, ErrNotAllowed = 9,
};
enum MonitorCode : uint8_t {
    EvStep = 1, EvState = 2, EvTempo = 3, EvRevision = 4, EvParam = 5, EvStats = 6, EvStepEdit = 7,
};

// CRC-8/SMBUS: polynomial 0x07, init 0, no reflection; check("123456789") = 0xF4.
inline uint8_t Crc8(const uint8_t* data, std::size_t size) {
    uint8_t crc = 0;
    for(std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for(int b = 0; b < 8; ++b)
            crc = uint8_t(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

// Builds a frame in place; all fields 7 bit, multi-byte values LSB first.
class Writer {
  public:
    Writer(uint8_t* buffer, std::size_t capacity) : data_(buffer), capacity_(capacity) {}
    void Begin(uint8_t type, uint8_t rid) {
        size_ = 0;
        ok_ = capacity_ >= kMaxFrame;
        for(uint8_t b : kHeader)
            Raw(b);
        Raw(kVersion);
        Raw(type & 0x7F);
        Raw(rid & 0x7F);
        Raw(0); // length, set in End()
    }
    void U7(uint32_t v) { Byte(v <= 0x7F ? uint8_t(v) : (ok_ = false, 0)); }
    void U14(uint32_t v) {
        if(v > 0x3FFF)
            ok_ = false;
        Byte(uint8_t(v & 0x7F));
        Byte(uint8_t((v >> 7) & 0x7F));
    }
    void U32(uint32_t v) {
        for(int i = 0; i < 5; ++i)
            Byte(uint8_t((v >> (7 * i)) & 0x7F));
    }
    void I32(int32_t v) { U32(uint32_t(v)); }
    void Str(const char* s, std::size_t limit) {
        std::size_t n = 0;
        while(s[n] && n < limit)
            ++n;
        U7(uint32_t(n));
        for(std::size_t i = 0; i < n; ++i)
            Byte(uint8_t(s[i]) & 0x7F);
    }
    // Returns the frame size, 0 if a field was out of range or the frame too long.
    std::size_t End() {
        const std::size_t payload = size_ - 8;
        if(!ok_ || payload > kMaxPayload || size_ + 3 > capacity_)
            return 0;
        data_[7] = uint8_t(payload);
        const uint8_t crc = Crc8(data_ + 4, size_ - 4);
        data_[size_++] = uint8_t(crc >> 7);
        data_[size_++] = uint8_t(crc & 0x7F);
        data_[size_++] = 0xF7;
        return size_;
    }
    bool Ok() const { return ok_; }

  private:
    void Raw(uint8_t b) {
        if(size_ < capacity_)
            data_[size_++] = b;
        else
            ok_ = false;
    }
    void Byte(uint8_t b) {
        if(size_ + 3 >= capacity_ || size_ - 8 >= kMaxPayload) {
            ok_ = false;
            return;
        }
        data_[size_++] = b;
    }
    uint8_t* data_;
    std::size_t capacity_, size_ = 0;
    bool ok_ = true;
};

class Reader {
  public:
    Reader(const uint8_t* payload, std::size_t size) : data_(payload), size_(size) {}
    uint8_t U7() { return pos_ < size_ ? data_[pos_++] : (ok_ = false, 0); }
    uint16_t U14() {
        const uint16_t lo = U7(), hi = U7();
        return uint16_t(lo | (hi << 7));
    }
    uint32_t U32() {
        uint32_t v = 0;
        for(int i = 0; i < 5; ++i) {
            const uint32_t part = U7();
            if(i == 4 && part > 0x0F)
                ok_ = false;
            v |= part << (7 * i);
        }
        return v;
    }
    int32_t I32() { return int32_t(U32()); }
    // Copies at most capacity-1 characters and terminates.
    void Str(char* out, std::size_t capacity) {
        const std::size_t n = U7();
        for(std::size_t i = 0; i < n; ++i) {
            const uint8_t c = U7();
            if(i + 1 < capacity)
                out[i] = char(c);
        }
        if(capacity)
            out[n < capacity ? n : capacity - 1] = 0;
    }
    bool Ok() const { return ok_; }
    bool Done() const { return ok_ && pos_ == size_; }
    std::size_t Remaining() const { return size_ - pos_; }

  private:
    const uint8_t* data_;
    std::size_t size_, pos_ = 0;
    bool ok_ = true;
};

enum class Verdict : uint8_t { Foreign, Ignore, Nak, Ok };

struct Parsed {
    Verdict verdict = Verdict::Foreign;
    uint8_t error = 0;
    uint8_t type = 0, rid = 0;
    const uint8_t* payload = nullptr;
    std::size_t size = 0;
    uint8_t crc = 0;
};

// Acceptance of one assembled frame F0 ... F7 in the documented order (PROTOCOL.md § 2).
// The MIDI parser has already removed interleaved real-time bytes.
inline Parsed Classify(const uint8_t* f, std::size_t n) {
    Parsed p;
    if(n < 5 || f[0] != kHeader[0] || f[1] != kHeader[1] || f[2] != kHeader[2] || f[3] != kHeader[3])
        return p;
    p.verdict = Verdict::Nak;
    p.rid = n > 6 ? (f[6] & 0x7F) : 0;
    p.type = n > 5 ? (f[5] & 0x7F) : 0;
    if(f[n - 1] != 0xF7) {
        p.error = ErrLength;
        return p;
    }
    for(std::size_t i = 1; i + 1 < n; ++i)
        if(f[i] & 0x80) {
            p.error = ErrCrc;
            return p;
        }
    if(n > kMaxFrame) {
        p.error = ErrLength;
        return p;
    }
    if(n >= 6 && (f[5] & kResponse)) {
        p.verdict = Verdict::Ignore;
        return p;
    }
    if(n < kOverhead || f[7] != n - kOverhead) {
        p.error = ErrLength;
        return p;
    }
    if(f[4] != kVersion) {
        p.error = ErrVersion;
        return p;
    }
    const uint8_t crc = Crc8(f + 4, n - 7);
    if(f[n - 3] != (crc >> 7) || f[n - 2] != (crc & 0x7F)) {
        p.error = ErrCrc;
        return p;
    }
    p.verdict = Verdict::Ok;
    p.payload = f + 8;
    p.size = n - kOverhead;
    p.crc = crc;
    return p;
}

// libDaisy hands SysEx over without F0/F7 (MidiEvent::sysex_data); this rebuilds the frame.
inline std::size_t FromSysExBody(const uint8_t* body, std::size_t size, uint8_t* out, std::size_t capacity) {
    if(size + 2 > capacity)
        return 0;
    out[0] = 0xF0;
    for(std::size_t i = 0; i < size; ++i)
        out[i + 1] = body[i];
    out[size + 1] = 0xF7;
    return size + 2;
}

} // namespace sq
