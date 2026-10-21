// SPDX-License-Identifier: MIT
#include "wav.h"
#include <cmath>
#include <cstring>

namespace looper::wav {
namespace {

constexpr uint16_t kPcm = 1, kFloat = 3, kExtensible = 0xFFFE;
// Tail of the KSDATAFORMAT_SUBTYPE GUIDs for PCM and IEEE float.
constexpr uint8_t kGuidTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                   0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
constexpr uint32_t kLoopChunkVersion = 1;

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void Put16(uint8_t*& p, uint16_t v) {
    *p++ = uint8_t(v);
    *p++ = uint8_t(v >> 8);
}
void Put32(uint8_t*& p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        *p++ = uint8_t(v >> (8 * i));
}
void PutTag(uint8_t*& p, const char* tag) {
    std::memcpy(p, tag, 4);
    p += 4;
}
bool Is(const uint8_t* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }
float Sanitize(float x) {
    if (!std::isfinite(x))
        return 0.f;
    return x > 1.f ? 1.f : (x < -1.f ? -1.f : x);
}
float Sample(const Info& info, const uint8_t* p) {
    if (info.format == kFloat) {
        float f;
        const uint32_t bits = U32(p);
        std::memcpy(&f, &bits, sizeof(f));
        return Sanitize(f);
    }
    if (info.bits == 16)
        return float(int16_t(U16(p))) * (1.f / 32768.f);
    const int32_t v = int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8;
    return float(v) * (1.f / 8388608.f);
}

}  // namespace

const char* Describe(Error error) {
    switch (error) {
    case Error::None: return "ok";
    case Error::Io: return "read error";
    case Error::NotRiff: return "not a RIFF/WAVE file";
    case Error::NoFormat: return "no format chunk";
    case Error::NoData: return "no data chunk";
    case Error::Truncated: return "truncated chunk";
    case Error::Unsupported: return "unsupported sample format";
    case Error::SampleRate: return "sample rate is not 48 kHz";
    case Error::Empty: return "no audio frames";
    case Error::TooLong: return "longer than the loop buffer";
    }
    return "unknown";
}

Error Parse(Source& source, uint32_t maxFrames, Info& info) {
    info = Info{};
    const uint32_t size = source.Size();
    uint8_t header[12];
    if (size < sizeof(header))
        return Error::NotRiff;
    if (!source.ReadAt(0, header, sizeof(header)))
        return Error::Io;
    if (!Is(header, "RIFF") || !Is(header + 8, "WAVE"))
        return Error::NotRiff;
    // The RIFF size may be wrong in either direction; the file size bounds all.
    const uint64_t riffEnd = uint64_t(U32(header + 4)) + 8;
    const uint64_t end = riffEnd < size ? riffEnd : size;
    bool haveFormat = false, haveData = false;
    uint64_t dataBytes = 0;
    uint64_t offset = 12;
    for (uint32_t chunk = 0; chunk < kMaxChunks && offset + 8 <= end; ++chunk) {
        uint8_t head[8];
        if (!source.ReadAt(uint32_t(offset), head, sizeof(head)))
            return Error::Io;
        const uint64_t bodySize = U32(head + 4);
        const uint64_t body = offset + 8;
        if (body + bodySize > end)
            return Error::Truncated;
        if (Is(head, "fmt ")) {
            if (bodySize < 16)
                return Error::NoFormat;
            uint8_t f[40] = {};
            const uint32_t n = bodySize >= 40 ? 40 : (bodySize >= 18 ? 18 : 16);
            if (!source.ReadAt(uint32_t(body), f, n))
                return Error::Io;
            info.format = U16(f);
            info.channels = U16(f + 2);
            info.sampleRate = U32(f + 4);
            info.blockAlign = U16(f + 12);
            info.bits = U16(f + 14);
            if (info.format == kExtensible) {
                if (n < 40 || std::memcmp(f + 26, kGuidTail, sizeof(kGuidTail)) != 0)
                    return Error::Unsupported;
                info.format = U16(f + 24);
            }
            haveFormat = true;
        } else if (Is(head, "data")) {
            info.dataOffset = uint32_t(body);
            dataBytes = bodySize;
            haveData = true;
        } else if (Is(head, "lprs") && bodySize >= 12) {
            uint8_t l[12];
            if (!source.ReadAt(uint32_t(body), l, sizeof(l)))
                return Error::Io;
            if (U32(l) == kLoopChunkVersion) {
                info.hasLoop = true;
                info.loopLength = U32(l + 4);
                info.tailLength = U32(l + 8);
            }
        }
        offset = body + bodySize + (bodySize & 1);  // chunks are word aligned
    }
    if (!haveFormat)
        return Error::NoFormat;
    if (!haveData)
        return Error::NoData;
    const bool pcm = info.format == kPcm && (info.bits == 16 || info.bits == 24);
    const bool flt = info.format == kFloat && info.bits == 32;
    if ((!pcm && !flt) || info.channels == 0 || info.channels > kMaxChannels ||
        info.blockAlign != uint32_t(info.channels) * (info.bits / 8))
        return Error::Unsupported;
    if (info.sampleRate != kSampleRate)
        return Error::SampleRate;
    const uint64_t frames = dataBytes / info.blockAlign;
    if (frames == 0)
        return Error::Empty;
    if (frames > maxFrames)
        return Error::TooLong;
    info.frames = uint32_t(frames);
    if (info.hasLoop && (info.loopLength == 0 || info.tailLength > kTailFrames ||
                         uint64_t(info.loopLength) + info.tailLength != frames))
        info.hasLoop = false;  // not ours or inconsistent: a plain loop
    return Error::None;
}

void Decode(const Info& info, const uint8_t* bytes, uint32_t frames, Frame* out) {
    const uint32_t step = info.bits / 8;
    for (uint32_t i = 0; i < frames; ++i) {
        const uint8_t* p = bytes + size_t(i) * info.blockAlign;
        const float left = Sample(info, p);
        out[i] = {left, info.channels > 1 ? Sample(info, p + step) : left};
    }
}

void WriteHeader(uint8_t* out, uint32_t length, uint32_t tail) {
    const uint32_t frames = length + tail;
    const uint32_t data = frames * kBytesPerFrame;
    uint8_t* p = out;
    PutTag(p, "RIFF");
    Put32(p, kHeaderBytes - 8 + data);
    PutTag(p, "WAVE");
    PutTag(p, "fmt ");
    Put32(p, 16);
    Put16(p, kFloat);
    Put16(p, 2);
    Put32(p, kSampleRate);
    Put32(p, kSampleRate * kBytesPerFrame);
    Put16(p, kBytesPerFrame);
    Put16(p, 32);
    PutTag(p, "lprs");
    Put32(p, 12);
    Put32(p, kLoopChunkVersion);
    Put32(p, length);
    Put32(p, tail);
    PutTag(p, "data");
    Put32(p, data);
}

void Encode(const Frame* in, uint32_t frames, uint8_t* out) {
    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t l, r;
        std::memcpy(&l, &in[i].left, 4);
        std::memcpy(&r, &in[i].right, 4);
        uint8_t* p = out + size_t(i) * kBytesPerFrame;
        Put32(p, l);
        Put32(p, r);
    }
}

}  // namespace looper::wav
