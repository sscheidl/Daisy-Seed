#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace physical_modeling {
constexpr size_t kBlockSize = 48;
constexpr size_t kQuantum = 12;
constexpr size_t kVoices = 4;
constexpr std::array<uint8_t, 16> kNotes = {
    60,62,64,65,67,69,71,72,72,74,76,77,79,81,83,84};
constexpr size_t kReverbWords = 32768;
inline float Unit(float x) { return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f; }
inline float Transpose(float x) { return 48.0f * Unit(x) - 24.0f; }
inline float Bend(unsigned x) {
    return x <= 8192 ? (float(x) - 8192.0f) / 4096.0f
                     : (float(x > 16383 ? 16383 : x) - 8192.0f) * (2.0f / 8191.0f);
}
// Driver UP=1, CENTER=0, DOWN=2 -> logical positions 1,2,3.
inline unsigned SwitchModel(bool bank, int position) {
    return (bank ? 3u : 0u) + (position == 1 ? 0u : position == 2 ? 2u : 1u);
}
}
