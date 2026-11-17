// SPDX-License-Identifier: MIT
// Clouds lookup tables, computed at Prepare time from the formulas in
// clouds/resources/lookup_tables.py (pichenettes/eurorack f4cfa7697095a28d292bff758beb51deb49b5a86)
// instead of importing the generated resources.cc. The grain-size table is expressed in seconds
// (Clouds: floor(1024 * 2^(4x)) samples at 32 kHz) and rendered at the internal rate, so a grain
// lasts the same time at 32 and 48 kHz.
#pragma once
#include <cmath>
#include <cstddef>

namespace cumulonimbus::clouds {

inline constexpr float kCloudsRate = 32000.f;

struct Tables {
    static constexpr std::size_t kWindowSize = 4096, kGrainSizeSize = 256, kSinSize = 1024;
    float window[kWindowSize + 1];       // lut_window: 1 - (cos(t * pi) + 1) / 2
    float grain_size[kGrainSizeSize + 1]; // lut_grain_size, in samples at `sample_rate`
    float sin[kSinSize + kSinSize / 4 + 1]; // lut_sin: one period plus a quarter (cosine)

    void Init(float sample_rate) {
        const double pi = 3.14159265358979323846;
        for(std::size_t i = 0; i <= kWindowSize; ++i)
            window[i] = float(1.0 - (std::cos(double(i) / kWindowSize * pi) + 1.0) / 2.0);
        const double scale = double(sample_rate) / kCloudsRate;
        for(std::size_t i = 0; i <= kGrainSizeSize; ++i) {
            const double clouds_samples = std::floor(1024.0 * std::exp2(double(i) / kGrainSizeSize * 4.0));
            // At 32 kHz this is exactly the Clouds table; elsewhere the same duration.
            grain_size[i] = float(std::floor(clouds_samples * scale));
        }
        for(std::size_t i = 0; i < sizeof(sin) / sizeof(sin[0]); ++i)
            sin[i] = float(std::sin(double(i) / kSinSize * pi * 2.0));
    }
};

// stmlib::Interpolate
inline float Interpolate(const float* table, float index, float size) {
    index *= size;
    const int integral = static_cast<int>(index);
    const float fractional = index - static_cast<float>(integral);
    const float a = table[integral];
    if(fractional == 0.0f)
        return a; // includes the final entry
    return a + (table[integral + 1] - a) * fractional;
}

} // namespace cumulonimbus::clouds
