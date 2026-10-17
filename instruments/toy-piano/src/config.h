#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace toy_piano {
constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kBlockSize = 48; // 1 kHz controls; matches Hardware's pot filters.
constexpr std::size_t kNumKeys = 16;
constexpr std::size_t kNumMidiVoices = 16; // Pool for notes arriving over USB MIDI.
constexpr std::size_t kNumVoices = kNumKeys + kNumMidiVoices;
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
// Small-signal gain per voice, sized for the 16 keys alone. The soft limiter in
// ToyPiano::Process() keeps the sum of all 32 voices below kOutputCeiling.
constexpr float kVoiceGain = kOutputCeiling / (kNumKeys * (1.0f + kSecondHarmonic));
// Above this phase increment (12 kHz at 48 kHz) the second harmonic would alias.
constexpr float kMaxHarmonicIncrement = 0.25f;
// USB MIDI. Keys are sent on channel 1 with a fixed velocity; any channel is received.
constexpr uint8_t kMidiOutChannel = 0;
constexpr uint8_t kMidiOutVelocity = 100;
constexpr std::size_t kMidiQueueSize = 64;     // Incoming notes; power of two.
constexpr std::size_t kMaxMidiPerBlock = 16;   // Bounds callback work.
constexpr std::size_t kMaxKeyMessagesPerPacket = 8; // Key changes sent per USB transfer.
constexpr uint32_t kLedIntervalMs = 10; // At most 100 I2C updates/second.
}
