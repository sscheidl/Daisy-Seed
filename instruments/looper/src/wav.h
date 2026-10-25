// SPDX-License-Identifier: MIT
// Bounds-checked WAV reading and writing for loops: every chunk is checked
// against the file size, every length against the buffer capacity.
#pragma once
#include "looper_engine.h"
#include "resample.h"
#include <cstddef>
#include <cstdint>

namespace looper::wav {

enum class Error : uint8_t {
    None,
    Io,           // the source could not be read
    NotRiff,      // no RIFF/WAVE header
    NoFormat,     // no usable fmt chunk
    NoData,       // no data chunk
    Truncated,    // a chunk extends past the end of the file
    Unsupported,  // not 16/24-bit PCM or 32-bit float
    SampleRate,   // not 44.1 or 48 kHz
    Empty,        // no frames
    TooLong,      // more frames than the buffer holds
};
const char* Describe(Error error);

// Random-access byte source, e.g. a file. ReadAt must fail rather than return
// fewer bytes.
class Source {
  public:
    virtual ~Source() = default;
    virtual uint32_t Size() const = 0;
    virtual bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) = 0;
};

struct Info {
    uint16_t format = 0;  // 1 = PCM, 3 = IEEE float
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t blockAlign = 0;  // bytes per frame
    uint32_t sampleRate = 0;
    uint32_t dataOffset = 0;
    uint32_t frames = 0;        // in the file, at sampleRate
    uint32_t outputFrames = 0;  // at 48 kHz after conversion; == frames at 48 kHz
    // Optional "lprs" chunk written by this looper: loop length and post-roll.
    // Valid only when loopLength + tailLength == frames.
    bool hasLoop = false;
    uint32_t loopLength = 0, tailLength = 0;
};

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kSampleRate44 = 44100;  // accepted for import, converted to 48 kHz
constexpr uint32_t kMaxChunks = 256;
constexpr uint16_t kMaxChannels = 8;  // bounds the bytes per frame

// Reads the header. Accepts PCM 16/24 bit and float 32 bit, also as
// WAVE_FORMAT_EXTENSIBLE, at 48 or 44.1 kHz with 1 to 8 channels (the first two
// are used), with up to maxFrames frames at 48 kHz (after conversion). The
// "lprs" chunk is only honored at 48 kHz.
Error Parse(Source& source, uint32_t maxFrames, Info& info);
// Converts `frames` whole frames of raw data to stereo frames: mono is
// duplicated, values are clamped to +-1 and non-finite floats become 0.
void Decode(const Info& info, const uint8_t* bytes, uint32_t frames, Frame* out);

// Loops are written as 32-bit float stereo with an "lprs" chunk, so a recall
// restores the stored loop exactly, including the crossfade post-roll.
constexpr uint32_t kHeaderBytes = 12 + 8 + 16 + 8 + 12 + 8;
constexpr uint32_t kBytesPerFrame = 8;
// Fills kHeaderBytes bytes for a loop of `length` frames plus `tail` frames.
void WriteHeader(uint8_t* out, uint32_t length, uint32_t tail);
void Encode(const Frame* in, uint32_t frames, uint8_t* out);

}  // namespace looper::wav
