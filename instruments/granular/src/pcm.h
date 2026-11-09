#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace neb {
constexpr unsigned kBlock = 256; // pinned libDaisy AudioHandle's supported maximum
constexpr unsigned kRate = 48000;
constexpr size_t kSdramBytes = 64u * 1024 * 1024;
constexpr size_t kReserveBytes = 8u * 1024 * 1024;
constexpr size_t kLiveBankBytes = 8u * 1024 * 1024;
constexpr size_t kLiveFrames = kLiveBankBytes / 4;
inline float Unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; }
inline int16_t Encode(float v) {
    if(!std::isfinite(v)) return 0;
    return static_cast<int16_t>(std::clamp(v, -1.f, 32767.f/32768.f)*32768.f);
}
struct PcmView {
    const int16_t* data = nullptr;
    uint32_t frames = 0, rate = kRate;
    uint16_t channels = 2;
    float At(uint32_t frame, unsigned channel) const {
        return data && frames ? data[size_t(frame % frames)*channels + (channels == 1 ? 0 : channel)]/32768.f : 0.f;
    }
    float Read(double frame, unsigned channel) const {
        if(!frames || !data || !std::isfinite(frame)) return 0.f;
        if(frames==1) return At(0,channel);
        if(frame<0 || frame>=frames) {
            frame = std::fmod(frame, double(frames));
            if(frame < 0) frame += frames;
        }
        const uint32_t i = static_cast<uint32_t>(frame);
        const float a = At(i,channel), b = At((i+1)%frames,channel);
        return a + float(frame-i)*(b-a);
    }
};
// One channel of a view, read with the same result as PcmView::Read for every position.
// A position strictly inside the buffer (the common case in the vocoder fill and the
// grains) skips the per-sample modulo and wrap test; everything else, including NaN,
// one-frame and empty views, goes through PcmView::Read. tests/test_nebulae.cpp
// compares both bit for bit.
class PcmReader {
  public:
    PcmReader(const PcmView& view, unsigned channel)
        : view_(view), channel_(channel),
          data_(view.data ? view.data + (view.channels == 1 ? 0 : channel) : nullptr),
          stride_(view.channels),
          last_(view.data && view.frames > 1 ? double(view.frames - 1) : -1.0) {}
    float operator()(double frame) const {
        if(frame >= 0 && frame < last_) {
            const uint32_t i = static_cast<uint32_t>(frame);
            const int16_t* p = data_ + size_t(i)*stride_;
            const float a = p[0]/32768.f, b = p[stride_]/32768.f;
            return a + float(frame-i)*(b-a);
        }
        return view_.Read(frame, channel_);
    }
  private:
    const PcmView& view_;
    unsigned channel_;
    const int16_t* data_;
    size_t stride_;
    double last_;
};
}
