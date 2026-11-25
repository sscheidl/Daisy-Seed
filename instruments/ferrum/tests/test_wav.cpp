// SPDX-License-Identifier: MIT
// WAV parser and writer: valid variants, every rejection, a round trip, and
// fuzzing by truncation and random mutation (run under ASan in test-sanitize).
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace looper;

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

struct MemorySource : wav::Source {
    const std::vector<uint8_t>& data;
    uint32_t outside = 0;
    explicit MemorySource(const std::vector<uint8_t>& d) : data(d) {}
    uint32_t Size() const override { return uint32_t(data.size()); }
    bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) override {
        if (uint64_t(offset) + bytes > data.size()) {
            ++outside;
            return false;
        }
        std::memcpy(destination, data.data() + offset, bytes);
        return true;
    }
};

using Bytes = std::vector<uint8_t>;
static void Add(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }
static void Add16(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
static void Add32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
static void Chunk(Bytes& b, const char* tag, const Bytes& body) {
    Add(b, tag);
    Add32(b, uint32_t(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1)
        b.push_back(0);
}
static Bytes Format(uint16_t tag, uint16_t channels, uint16_t bits, uint32_t rate,
                    bool extensible = false, uint16_t subformat = 1) {
    Bytes f;
    Add16(f, extensible ? 0xFFFE : tag);
    Add16(f, channels);
    Add32(f, rate);
    Add32(f, rate * channels * bits / 8);
    Add16(f, channels * bits / 8);
    Add16(f, bits);
    if (extensible) {
        Add16(f, 22);
        Add16(f, bits);
        Add32(f, 3);
        Add16(f, subformat);
        const uint8_t tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        f.insert(f.end(), tail, tail + 14);
    }
    return f;
}
// Sample value v (-1..1) for channel c of frame i.
static float Value(uint32_t i, uint32_t c) { return float((int(i * 7 + c * 3) % 31) - 15) / 16.f; }
static Bytes Data(uint16_t tag, uint16_t channels, uint16_t bits, uint32_t frames) {
    Bytes d;
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < channels; ++c) {
            const float v = Value(i, c);
            if (tag == 3) {
                uint32_t u;
                std::memcpy(&u, &v, 4);
                Add32(d, u);
            } else if (bits == 16) {
                Add16(d, uint16_t(int16_t(v * 32768.f)));
            } else {
                const int32_t s = int32_t(v * 8388608.f);
                d.push_back(uint8_t(s));
                d.push_back(uint8_t(s >> 8));
                d.push_back(uint8_t(s >> 16));
            }
        }
    return d;
}
static Bytes Riff(const std::vector<std::pair<const char*, Bytes>>& chunks) {
    Bytes body;
    Add(body, "WAVE");
    for (const auto& [tag, data] : chunks)
        Chunk(body, tag, data);
    Bytes b;
    Add(b, "RIFF");
    Add32(b, uint32_t(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
static Bytes File(uint16_t tag, uint16_t channels, uint16_t bits, uint32_t frames,
                  uint32_t rate = 48000) {
    return Riff({{"fmt ", Format(tag, channels, bits, rate)}, {"data", Data(tag, channels, bits, frames)}});
}

static wav::Error ParseBytes(const Bytes& b, wav::Info& info, uint32_t max = 100000) {
    MemorySource s(b);
    const wav::Error e = wav::Parse(s, max, info);
    CHECK(s.outside == 0);  // the parser never asks for bytes past the end
    if (e == wav::Error::None) {
        CHECK(uint64_t(info.dataOffset) + uint64_t(info.frames) * info.blockAlign <= b.size());
        CHECK(info.frames > 0 && info.frames <= max);
    }
    return e;
}
static bool DecodesTo(const Bytes& b, const wav::Info& info, uint32_t channels, float tolerance) {
    std::vector<Frame> out(info.frames);
    wav::Decode(info, b.data() + info.dataOffset, info.frames, out.data());
    for (uint32_t i = 0; i < info.frames; ++i) {
        const float l = Value(i, 0), r = channels > 1 ? Value(i, 1) : l;
        if (std::fabs(out[i].left - l) > tolerance || std::fabs(out[i].right - r) > tolerance)
            return false;
    }
    return true;
}

static void TestValid() {
    wav::Info info;
    struct Case {
        uint16_t tag, channels, bits;
    };
    for (const Case c : {Case{1, 1, 16}, Case{1, 2, 16}, Case{1, 2, 24}, Case{3, 1, 32},
                         Case{3, 2, 32}, Case{1, 4, 16}, Case{1, 8, 24}}) {
        const Bytes b = File(c.tag, c.channels, c.bits, 1000);
        CHECK(ParseBytes(b, info) == wav::Error::None);
        CHECK(info.frames == 1000 && info.channels == c.channels && !info.hasLoop);
        CHECK(DecodesTo(b, info, c.channels, c.tag == 3 ? 0.f : 1e-4f));
    }
    for (uint16_t sub : {uint16_t(1), uint16_t(3)}) {  // WAVE_FORMAT_EXTENSIBLE
        const uint16_t bits = sub == 3 ? 32 : 24;
        const Bytes b = Riff({{"fmt ", Format(sub, 2, bits, 48000, true, sub)}, {"data", Data(sub, 2, bits, 500)}});
        CHECK(ParseBytes(b, info) == wav::Error::None && info.format == sub);
        CHECK(DecodesTo(b, info, 2, 1e-4f));
    }
    // Unknown and odd-sized chunks are skipped with their pad byte; fmt may follow data.
    const Bytes odd = Riff({{"LIST", Bytes(13, 'x')}, {"data", Data(1, 2, 16, 300)},
                            {"junk", Bytes(1, 0)}, {"fmt ", Format(1, 2, 16, 48000)}});
    CHECK(ParseBytes(odd, info) == wav::Error::None && info.frames == 300);
    CHECK(DecodesTo(odd, info, 2, 1e-4f));
    // A RIFF size larger than the file is bounded by the file.
    Bytes big = File(1, 2, 16, 200);
    big[4] = big[5] = big[6] = big[7] = 0xFF;
    CHECK(ParseBytes(big, info) == wav::Error::None && info.frames == 200);
    // Out-of-range float samples are clamped, non-finite ones zeroed.
    Bytes wild;
    for (float v : {2.f, -3.f, NAN, INFINITY}) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        Add32(wild, u);
    }
    const Bytes w = Riff({{"fmt ", Format(3, 1, 32, 48000)}, {"data", wild}});
    CHECK(ParseBytes(w, info) == wav::Error::None);
    Frame out[4];
    wav::Decode(info, w.data() + info.dataOffset, 4, out);
    CHECK(out[0].left == 1.f && out[1].left == -1.f && out[2].left == 0.f && out[3].left == 0.f);
    std::printf("%s valid: PCM16/24, float, mono to 8 channels, extensible, odd chunks, clamping\n",
                failures ? "FAIL" : "PASS");
}

static void TestRejected() {
    wav::Info info;
    const int before = failures;
    auto expect = [&](const Bytes& b, wav::Error e, uint32_t max = 100000) {
        const wav::Error got = ParseBytes(b, info, max);
        if (got != e)
            std::printf("  expected '%s', got '%s'\n", wav::Describe(e), wav::Describe(got));
        CHECK(got == e);
    };
    expect(Bytes{}, wav::Error::NotRiff);
    expect(Bytes(11, 0), wav::Error::NotRiff);
    Bytes notWave = File(1, 2, 16, 10);
    std::memcpy(notWave.data() + 8, "AVI ", 4);
    expect(notWave, wav::Error::NotRiff);
    expect(Riff({{"data", Data(1, 2, 16, 10)}}), wav::Error::NoFormat);
    expect(Riff({{"fmt ", Format(1, 2, 16, 48000)}}), wav::Error::NoData);
    expect(Riff({{"fmt ", Bytes(14, 0)}, {"data", Data(1, 2, 16, 10)}}), wav::Error::NoFormat);
    expect(File(1, 2, 8, 10), wav::Error::Unsupported);
    expect(File(1, 2, 32, 10), wav::Error::Unsupported);
    expect(File(3, 2, 16, 10), wav::Error::Unsupported);
    expect(File(2, 2, 16, 10), wav::Error::Unsupported);  // ADPCM
    expect(File(1, 9, 16, 10), wav::Error::Unsupported);
    expect(File(1, 0, 16, 0), wav::Error::Unsupported);
    Bytes align = File(1, 2, 16, 10);
    align[12 + 8 + 12] = 3;  // blockAlign != channels x bytes
    expect(align, wav::Error::Unsupported);
    Bytes guid = Riff({{"fmt ", Format(1, 2, 16, 48000, true, 1)}, {"data", Data(1, 2, 16, 10)}});
    guid[12 + 8 + 30] ^= 1;  // wrong subformat GUID
    expect(guid, wav::Error::Unsupported);
    expect(File(1, 2, 16, 10, 96000), wav::Error::SampleRate);
    expect(File(1, 2, 16, 10, 88200), wav::Error::SampleRate);
    expect(File(1, 2, 16, 10, 32000), wav::Error::SampleRate);
    expect(File(1, 2, 16, 0), wav::Error::Empty);
    expect(File(1, 2, 16, 0, 44100), wav::Error::Empty);
    expect(File(1, 2, 16, 1001), wav::Error::TooLong, 1000);
    CHECK(ParseBytes(File(1, 2, 16, 1000), info, 1000) == wav::Error::None);  // exactly the limit
    CHECK(info.outputFrames == info.frames);
    // 44.1 kHz is converted: the limit applies to the frames at 48 kHz.
    CHECK(ParseBytes(File(1, 2, 16, 919, 44100), info, 1000) == wav::Error::None);
    CHECK(info.sampleRate == 44100 && info.frames == 919 && info.outputFrames == 1000);
    expect(File(1, 2, 16, 920, 44100), wav::Error::TooLong, 1000);
    Bytes truncated = File(1, 2, 16, 100);
    truncated.resize(truncated.size() - 1);
    expect(truncated, wav::Error::Truncated);
    Bytes huge = File(1, 2, 16, 100);
    const size_t dataSize = huge.size() - 400 - 4;
    huge[dataSize] = huge[dataSize + 1] = huge[dataSize + 2] = huge[dataSize + 3] = 0xFF;
    expect(huge, wav::Error::Truncated);
    // A loop chunk that does not add up is ignored.
    Bytes lp;
    Add32(lp, 1);
    Add32(lp, 90);
    Add32(lp, 20);
    CHECK(ParseBytes(Riff({{"fmt ", Format(3, 2, 32, 48000)}, {"lprs", lp}, {"data", Data(3, 2, 32, 100)}}),
                     info) == wav::Error::None);
    CHECK(!info.hasLoop);
    // A loop chunk at 44.1 kHz does not describe the converted frames: ignored.
    Bytes lp44;
    Add32(lp44, 1);
    Add32(lp44, 100);
    Add32(lp44, 0);
    CHECK(ParseBytes(Riff({{"fmt ", Format(3, 2, 32, 44100)}, {"lprs", lp44}, {"data", Data(3, 2, 32, 100)}}),
                     info) == wav::Error::None);
    CHECK(!info.hasLoop && info.outputFrames == 108);
    std::printf("%s rejected: header, format, rate, empty, too long, truncated, bad loop chunk\n",
                failures == before ? "PASS" : "FAIL");
}

static void TestRoundTrip() {
    const int before = failures;
    const uint32_t length = 3000, tail = kTailFrames;
    std::vector<Frame> frames(length + tail);
    for (uint32_t i = 0; i < frames.size(); ++i)
        frames[i] = {Value(i, 0) * .9f, -Value(i, 1) * .3f};
    Bytes b(wav::kHeaderBytes + frames.size() * wav::kBytesPerFrame);
    wav::WriteHeader(b.data(), length, tail);
    wav::Encode(frames.data(), uint32_t(frames.size()), b.data() + wav::kHeaderBytes);
    wav::Info info;
    CHECK(ParseBytes(b, info) == wav::Error::None);
    CHECK(info.hasLoop && info.loopLength == length && info.tailLength == tail);
    CHECK(info.format == 3 && info.channels == 2 && info.dataOffset == wav::kHeaderBytes);
    std::vector<Frame> back(info.frames);
    wav::Decode(info, b.data() + info.dataOffset, info.frames, back.data());
    bool exact = true;
    for (uint32_t i = 0; i < frames.size(); ++i)
        exact &= std::memcmp(&back[i], &frames[i], sizeof(Frame)) == 0;
    CHECK(exact);
    std::printf("%s round trip: float32 stereo with loop chunk restores the frames bit for bit\n",
                failures == before ? "PASS" : "FAIL");
}

static void TestFuzz() {
    const int before = failures;
    // Every truncation of valid files.
    Bytes looped(wav::kHeaderBytes + 300 * wav::kBytesPerFrame);
    std::vector<Frame> frames(300, Frame{.5f, -.5f});
    wav::WriteHeader(looped.data(), 200, 100);
    wav::Encode(frames.data(), 300, looped.data() + wav::kHeaderBytes);
    const Bytes ext = Riff({{"LIST", Bytes(7, 'a')},
                            {"fmt ", Format(1, 2, 24, 48000, true, 1)},
                            {"data", Data(1, 2, 24, 100)}});
    uint32_t accepted = 0;
    for (const Bytes* original : {static_cast<const Bytes*>(&looped), &ext})
        for (size_t n = 0; n <= original->size(); ++n) {
            const Bytes b(original->begin(), original->begin() + long(n));
            wav::Info info;
            accepted += ParseBytes(b, info) == wav::Error::None;
        }
    CHECK(accepted == 2);  // only the complete files
    // Random mutations of the headers and sizes.
    uint32_t seed = 12345, valid = 0;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int round = 0; round < 40000; ++round) {
        Bytes b = (round & 1) ? looped : ext;
        const int edits = 1 + int(next() % 6);
        for (int e = 0; e < edits; ++e) {
            const size_t at = next() % std::min<size_t>(b.size(), 120);
            b[at] = (next() & 3) == 0 ? 0xFF : uint8_t(next());
        }
        if (next() % 4 == 0)
            b.resize(next() % b.size());
        wav::Info info;
        if (ParseBytes(b, info, 1000) == wav::Error::None) {
            ++valid;
            std::vector<Frame> out(info.frames);  // decoding stays inside the file
            wav::Decode(info, b.data() + info.dataOffset, info.frames, out.data());
            for (const Frame& f : out)
                CHECK(std::isfinite(f.left) && std::fabs(f.left) <= 1.f && std::fabs(f.right) <= 1.f);
        }
    }
    CHECK(valid > 0);
    std::printf("%s fuzz: all truncations and 40000 mutations stay in bounds (%u still valid)\n",
                failures == before ? "PASS" : "FAIL", valid);
}

int main() {
    TestValid();
    TestRejected();
    TestRoundTrip();
    TestFuzz();
    if (failures) {
        std::printf("FAILED: %d WAV check(s)\n", failures);
        return 1;
    }
    std::printf("All WAV tests passed\n");
    return 0;
}
