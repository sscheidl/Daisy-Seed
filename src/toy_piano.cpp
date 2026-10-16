#include "toy_piano.h"
#include "Utility/dsp.h" // DaisySP's MIDI conversion, also used in host tests.
#include <algorithm>
#include <cmath>

namespace toy_piano {
float MidiToFrequency(uint8_t note) { return daisysp::mtof(static_cast<float>(note)); }

float LogTime(float position, float minimum, float maximum) {
    position = std::max(0.0f, std::min(1.0f, position));
    return minimum * std::pow(maximum / minimum, position);
}

float PotFilter::Process(float input) {
    input = std::max(0.0f, std::min(1.0f, input));
    if(!initialized_) {
        smoothed_ = stable_ = input;
        initialized_ = true;
    }
    // 1 - exp(-1 ms / 20 ms), fixed 1 kHz call rate.
    smoothed_ += 0.0487705755f * (input - smoothed_);
    if(std::fabs(smoothed_ - stable_) > 0.001f)
        stable_ = smoothed_;
    // Reach exact time endpoints despite ADC endpoint tolerance.
    return stable_ < 0.003f ? 0.0f : (stable_ > 0.997f ? 1.0f : stable_);
}

void AdEnvelope::Trigger(float attack, float decay, float sample_rate) {
    attack_samples_ = std::max<uint32_t>(1, static_cast<uint32_t>(attack * sample_rate + 0.5f));
    decay_samples_ = std::max<uint32_t>(1, static_cast<uint32_t>(decay * sample_rate + 0.5f));
    attack_step_ = 1.0f / attack_samples_;
    decay_step_ = 1.0f / decay_samples_;
    start_level_ = level_; // Retrigger without an amplitude discontinuity.
    position_ = 0;
    stage_ = Stage::Attack;
}

float AdEnvelope::Process() {
    if(stage_ == Stage::Attack) {
        const float t = std::min(1.0f, ++position_ * attack_step_);
        const float smooth = t * t * (3.0f - 2.0f * t);
        level_ = start_level_ + (1.0f - start_level_) * smooth;
        if(position_ >= attack_samples_) {
            level_ = 1.0f;
            position_ = 0;
            stage_ = Stage::Decay;
        }
    } else if(stage_ == Stage::Decay) {
        const float remaining = std::max(0.0f, 1.0f - ++position_ * decay_step_);
        level_ = remaining * remaining * remaining;
        if(position_ >= decay_samples_) {
            level_ = 0.0f;
            stage_ = Stage::Idle;
        }
    }
    return level_;
}

void ToyPiano::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    for(std::size_t i = 0; i < kTableSize; ++i)
        sine_[i] = std::sin(6.28318530718f * static_cast<float>(i) / kTableSize);
    sine_[kTableSize] = sine_[0];
    for(std::size_t i = 0; i < kNumKeys; ++i) {
        voices_[i] = Voice{};
        voices_[i].increment = MidiToFrequency(kMidiNotes[i]) / sample_rate_;
    }
}

void ToyPiano::Trigger(std::size_t key, float attack, float decay) {
    if(key < kNumKeys)
        voices_[key].envelope.Trigger(attack, decay, sample_rate_);
}

float ToyPiano::Sine(float phase) const {
    const float index = phase * kTableSize;
    const auto i = static_cast<std::size_t>(index);
    return sine_[i] + (sine_[i + 1] - sine_[i]) * (index - i);
}

float ToyPiano::Process() {
    float sum = 0.0f;
    for(auto& voice : voices_) {
        if(!voice.envelope.Active())
            continue;
        float second_phase = 2.0f * voice.phase;
        if(second_phase >= 1.0f)
            second_phase -= 1.0f;
        const float wave = Sine(voice.phase) + kSecondHarmonic * Sine(second_phase);
        sum += wave * voice.envelope.Process();
        // Never reset phase on retrigger. No transcendental work per sample.
        voice.phase += voice.increment;
        if(voice.phase >= 1.0f)
            voice.phase -= 1.0f;
    }
    // Fixed worst-case normalization: |sum| <= 0.8 even with 16 coherent voices.
    return sum * kVoiceGain;
}
}
