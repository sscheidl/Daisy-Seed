#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace toy_piano {
constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kBlockSize = 48; // 1 kHz controls; matches Hardware's pot filters.
constexpr std::size_t kNumKeys = 16;
// Physical S1..S16, in the two rows printed on the Desktop Dev Kit.
constexpr std::array<uint8_t, kNumKeys> kMidiNotes = {
    60, 62, 64, 65, 67, 69, 71, 72,
    72, 74, 76, 77, 79, 81, 83, 84};
constexpr float kAttackMin = 0.002f;
constexpr float kAttackMax = 0.300f;
constexpr float kDecayMin = 0.050f;
constexpr float kDecayMax = 3.000f;
constexpr float kSecondHarmonic = 0.12f; // Set to zero for a pure sine.
constexpr float kOutputCeiling = 0.8f;
constexpr float kVoiceGain = kOutputCeiling / (kNumKeys * (1.0f + kSecondHarmonic));
constexpr uint32_t kLedIntervalMs = 10; // At most 100 I2C updates/second.
}
