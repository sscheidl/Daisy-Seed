#pragma once
// SysEx codec of PROTOCOL.md v1. Shared by firmware, host tests and the editor.
// A "body" is everything between F0 and F7, as libDaisy and JUCE deliver it.
#include <cstddef>
#include <cstdint>

namespace mod::proto {
constexpr uint8_t kManufacturer = 0x7D; // non-commercial / development
constexpr uint8_t kProject[2] = {0x4D, 0x44}; // "MD"
constexpr uint8_t kVersion = 1, kVersionMinor = 0;
constexpr uint8_t kBroadcast = 0x7F, kReplyBit = 0x40, kNak = 0x7F;
constexpr std::size_t kMaxFrame = 120;     // including F0 and F7 (libDaisy keeps 128 body bytes)
constexpr std::size_t kHeader = 8;         // 7D 4D 44 ver dev cmd seq len
constexpr std::size_t kMaxPayload = kMaxFrame - 2 - kHeader - 1; // 109

enum Command : uint8_t {
    Identify = 0x01, GetTarget = 0x02, GetAllowed = 0x03,
    GetMatrix = 0x10, SetMatrix = 0x11, SetCell = 0x12, ClearMatrix = 0x13,
    GetSource = 0x18, SetSource = 0x19, GetName = 0x1A, SetName = 0x1B,
    Store = 0x20, Recall = 0x21, GetSlotInfo = 0x22, SetPowerOn = 0x23,
    GetCalibration = 0x24, SetCalibration = 0x25,
    Monitor = 0x30, GetStats = 0x31, ResetStats = 0x32, SetScenario = 0x33,
    // Unsolicited monitor data. Not 0x70: that is the reply to Monitor (0x30 | 0x40).
    MonitorFrame = 0x7E,
};
enum Error : uint8_t {
    ErrChecksum = 1, ErrLength = 2, ErrUnknown = 3, ErrRange = 4, ErrBusy = 5, ErrVersion = 6,
    ErrLimit = 7, ErrNotAllowed = 8, ErrNotImplemented = 9, ErrStorage = 10, ErrSchema = 11,
};

// Roland-style: all bytes from <ver> to the end of the payload plus chk sum to 0 mod 128.
inline uint8_t Checksum(const uint8_t* data, std::size_t size) {
    unsigned sum = 0;
    for(std::size_t i = 0; i < size; ++i)
        sum += data[i];
    return uint8_t((128 - (sum & 127)) & 127);
}

constexpr std::size_t PackedSize(std::size_t bytes) { return bytes + (bytes + 6) / 7; }

class Writer {
  public:
    Writer(uint8_t* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity) {}
    void U7(unsigned v) { Put(uint8_t(v & 127)); }
    void U14(unsigned v) { U7(v); U7(v >> 7); }
    void U32(uint32_t v) {
        for(int i = 0; i < 5; ++i, v >>= 7)
            U7(v);
    }
    void Str(const char* s, std::size_t max) {
        std::size_t n = 0;
        while(s && s[n] && n < max)
            ++n;
        U7(unsigned(n));
        for(std::size_t i = 0; i < n; ++i)
            U7(uint8_t(s[i]));
    }
    // 8-in-7: an MSB byte precedes every group of up to 7 data bytes.
    void Packed(const uint8_t* data, std::size_t size) {
        for(std::size_t g = 0; g < size; g += 7) {
            uint8_t msbs = 0;
            for(std::size_t i = 0; i < 7 && g + i < size; ++i)
                msbs |= uint8_t((data[g + i] >> 7) << i);
            U7(msbs);
            for(std::size_t i = 0; i < 7 && g + i < size; ++i)
                U7(data[g + i]);
        }
    }
    void Mask64(uint64_t mask) {
        for(int i = 0; i < 10; ++i, mask >>= 7)
            U7(unsigned(mask & 127));
    }
    std::size_t Size() const { return size_; }
    const uint8_t* Data() const { return buffer_; }
    bool Ok() const { return ok_; }
  private:
    void Put(uint8_t b) {
        if(size_ < capacity_)
            buffer_[size_++] = b;
        else
            ok_ = false;
    }
    uint8_t* buffer_;
    std::size_t capacity_, size_ = 0;
    bool ok_ = true;
};

class Reader {
  public:
    Reader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    unsigned U7() {
        if(pos_ >= size_) {
            ok_ = false;
            return 0;
        }
        return data_[pos_++] & 127;
    }
    unsigned U14() {
        const unsigned lo = U7();
        return lo | (U7() << 7);
    }
    uint32_t U32() {
        uint32_t v = 0;
        for(int i = 0; i < 5; ++i)
            v |= uint32_t(U7()) << (7 * i);
        return v;
    }
    // Returns the length; `out` gets a zero-padded copy of at most `max` characters.
    std::size_t Str(char* out, std::size_t max) {
        const std::size_t n = U7();
        if(n > max)
            ok_ = false;
        for(std::size_t i = 0; i < max; ++i)
            out[i] = 0;
        for(std::size_t i = 0; i < n; ++i) {
            const char c = char(U7());
            if(i < max)
                out[i] = c;
        }
        return n;
    }
    void Packed(uint8_t* out, std::size_t size) {
        for(std::size_t g = 0; g < size; g += 7) {
            const unsigned msbs = U7();
            for(std::size_t i = 0; i < 7 && g + i < size; ++i)
                out[g + i] = uint8_t(U7() | (((msbs >> i) & 1u) << 7));
        }
    }
    uint64_t Mask64() {
        uint64_t mask = 0;
        for(int i = 0; i < 10; ++i)
            mask |= uint64_t(U7()) << (7 * i);
        return mask;
    }
    bool Ok() const { return ok_; }
    bool AtEnd() const { return pos_ == size_; }
  private:
    const uint8_t* data_;
    std::size_t size_, pos_ = 0;
    bool ok_ = true;
};

struct Frame {
    uint8_t version = 0, device = 0, command = 0, seq = 0, length = 0;
    const uint8_t* payload = nullptr;
};

enum class Parse { NotOurs, Short, Ok };

// Recognizes the manufacturer/project prefix and extracts the header fields.
inline Parse ParseHeader(const uint8_t* body, std::size_t size, Frame& f) {
    if(size < 3 || body[0] != kManufacturer || body[1] != kProject[0] || body[2] != kProject[1])
        return Parse::NotOurs;
    if(size < kHeader + 1)
        return Parse::Short;
    f.version = body[3];
    f.device = body[4];
    f.command = body[5];
    f.seq = body[6];
    f.length = body[7];
    f.payload = body + kHeader;
    return Parse::Ok;
}

// 0 or ErrVersion / ErrLength / ErrChecksum. A body truncated by the 128-byte
// libDaisy buffer never matches its own length field.
inline uint8_t Verify(const uint8_t* body, std::size_t size, const Frame& f) {
    if(f.version != kVersion)
        return ErrVersion;
    if(size + 2 > kMaxFrame || f.length > kMaxPayload || size != kHeader + f.length + 1)
        return ErrLength;
    if(Checksum(body + 3, kHeader - 3 + f.length) != body[kHeader + f.length])
        return ErrChecksum;
    return 0;
}

// Complete message F0 … F7; returns its size, 0 if the payload is too long.
inline std::size_t BuildFrame(uint8_t (&out)[kMaxFrame], uint8_t device, uint8_t command, uint8_t seq,
                              const uint8_t* payload, std::size_t length) {
    if(length > kMaxPayload)
        return 0;
    std::size_t n = 0;
    out[n++] = 0xF0;
    out[n++] = kManufacturer;
    out[n++] = kProject[0];
    out[n++] = kProject[1];
    out[n++] = kVersion;
    out[n++] = device & 127;
    out[n++] = command & 127;
    out[n++] = seq & 127;
    out[n++] = uint8_t(length);
    for(std::size_t i = 0; i < length; ++i)
        out[n++] = payload[i] & 127;
    out[n] = Checksum(out + 4, n - 4);
    ++n;
    out[n++] = 0xF7;
    return n;
}

// s14 offset binary for -1..+1 values (monitor, calibration).
inline unsigned EncodeSigned(float v) {
    const float scaled = v * 8191.0f;
    const int i = int(scaled < 0 ? scaled - 0.5f : scaled + 0.5f);
    const int clamped = i < -8192 ? -8192 : (i > 8191 ? 8191 : i);
    return unsigned(clamped + 8192);
}
inline float DecodeSigned(unsigned v) { return float(int(v & 16383) - 8192) / 8191.0f; }
}
