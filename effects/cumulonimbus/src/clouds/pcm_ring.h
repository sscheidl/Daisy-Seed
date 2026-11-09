// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// PCM16 recording ring with interpolated reads.
//
// Derived from clouds/dsp/audio_buffer.h at pichenettes/eurorack
// f4cfa7697095a28d292bff758beb51deb49b5a86 (see ../../UPSTREAM.md). Local changes:
// * 16-bit resolution only (the 8-bit and mu-law paths are not built here).
// * Plain float input pointers; no stmlib include (Clip16 is local).
// * Fix: the unfreeze crossfade read tail_[kCrossFadeSize] (one past the end) on its last
//   sample; it now reads indices 0..kCrossFadeSize-1.
// * Reads wrap any index in [0, 2 * size) and fold larger ones defensively.
#pragma once
#include <algorithm>
#include <cstdint>

namespace cumulonimbus::clouds {

constexpr int32_t kCrossFadeSize = 256;
constexpr int32_t kInterpolationTail = 8;

enum InterpolationMethod { INTERPOLATION_ZOH, INTERPOLATION_LINEAR, INTERPOLATION_HERMITE };

inline int16_t Clip16(int32_t x) { return int16_t(x < -32768 ? -32768 : (x > 32767 ? 32767 : x)); }

class PcmRing {
  public:
    // `frames` includes kInterpolationTail guard samples; `tail` holds kCrossFadeSize samples.
    void Init(int16_t* buffer, int32_t frames, int16_t* tail) {
        s16_ = buffer;
        size_ = frames - kInterpolationTail;
        write_head_ = 0;
        crossfade_counter_ = 0;
        std::fill(&s16_[0], &s16_[frames], int16_t(0));
        tail_ = tail;
        std::fill(&tail_[0], &tail_[kCrossFadeSize], int16_t(0));
    }

    inline void Write(float in) {
        s16_[write_head_] = Clip16(static_cast<int32_t>(in * 32768.0f));
        if(write_head_ < kInterpolationTail)
            s16_[write_head_ + size_] = s16_[write_head_];
        ++write_head_;
        if(write_head_ >= size_)
            write_head_ = 0;
    }

    // Clouds' freeze-aware write. While `write` is false the ring keeps its contents and the
    // first kCrossFadeSize incoming samples are kept, so recording resumes with a crossfade.
    inline void WriteFade(const float* in, int32_t size, int32_t stride, bool write) {
        if(!write) {
            while(size-- && crossfade_counter_ < kCrossFadeSize) {
                tail_[crossfade_counter_++] = Clip16(static_cast<int32_t>(*in * 32767.0f));
                in += stride;
            }
        } else if(!crossfade_counter_ && write_head_ >= kInterpolationTail && write_head_ < (size_ - size)) {
            // Fast write routine for the most common case.
            while(size--) {
                s16_[write_head_] = Clip16(static_cast<int32_t>(*in * 32767.0f));
                ++write_head_;
                in += stride;
            }
        } else {
            while(size--) {
                float sample = *in;
                if(crossfade_counter_) {
                    --crossfade_counter_;
                    const float tail_sample = tail_[kCrossFadeSize - 1 - crossfade_counter_];
                    const float gain = crossfade_counter_ * (1.0f / float(kCrossFadeSize));
                    sample += (tail_sample / 32768.0f - sample) * gain;
                }
                Write(sample);
                in += stride;
            }
        }
    }

    template <InterpolationMethod method> inline float Read(int32_t integral, uint16_t fractional) const {
        if(method == INTERPOLATION_ZOH)
            return ReadZOH(integral);
        if(method == INTERPOLATION_LINEAR)
            return ReadLinear(integral, fractional);
        return ReadHermite(integral, fractional);
    }

    inline float ReadZOH(int32_t integral) const { return s16_[Wrap(integral)] * (1.0f / 32768.0f); }

    inline float ReadLinear(int32_t integral, uint16_t fractional) const {
        integral = Wrap(integral);
        const float t = static_cast<float>(fractional) / 65536.0f;
        const float x0 = s16_[integral], x1 = s16_[integral + 1];
        return (x0 + (x1 - x0) * t) * (1.0f / 32768.0f);
    }

    inline float ReadHermite(int32_t integral, uint16_t fractional) const {
        integral = Wrap(integral);
        const float t = static_cast<float>(fractional) / 65536.0f;
        const float xm1 = s16_[integral], x0 = s16_[integral + 1];
        const float x1 = s16_[integral + 2], x2 = s16_[integral + 3];
        // Laurent de Soras's Hermite interpolator.
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b_neg = w + a;
        return ((((a * t) - b_neg) * t + c) * t + x0) * (1.0f / 32768.0f);
    }

    inline int32_t size() const { return size_; }
    inline int32_t head() const { return write_head_; }

  private:
    inline int32_t Wrap(int32_t i) const {
        if(i >= size_) {
            i -= size_;
            if(i >= size_) // only reachable with grains longer than the ring (tiny test rings)
                i %= size_;
        } else if(i < 0) {
            i %= size_;
            if(i < 0) i += size_;
        }
        return i;
    }

    int16_t* s16_ = nullptr;
    int16_t* tail_ = nullptr;
    int32_t size_ = 1;
    int32_t write_head_ = 0;
    int32_t crossfade_counter_ = 0;
};

} // namespace cumulonimbus::clouds
