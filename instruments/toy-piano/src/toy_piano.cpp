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

void AdEnvelope::Trigger(float attack, float decay, float sample_rate, float peak) {
    attack_samples_ = std::max<uint32_t>(1, static_cast<uint32_t>(attack * sample_rate + 0.5f));
    decay_samples_ = std::max<uint32_t>(1, static_cast<uint32_t>(decay * sample_rate + 0.5f));
    attack_step_ = 1.0f / attack_samples_;
    decay_step_ = 1.0f / decay_samples_;
    start_level_ = level_; // Retrigger without an amplitude discontinuity.
    peak_ = peak;
    position_ = 0;
    stage_ = Stage::Attack;
}

float AdEnvelope::Process() {
    if(stage_ == Stage::Attack) {
        const float t = std::min(1.0f, ++position_ * attack_step_);
        const float smooth = t * t * (3.0f - 2.0f * t);
        level_ = start_level_ + (peak_ - start_level_) * smooth;
        if(position_ >= attack_samples_) {
            level_ = peak_;
            position_ = 0;
            stage_ = Stage::Decay;
        }
    } else if(stage_ == Stage::Decay) {
        const float remaining = std::max(0.0f, 1.0f - ++position_ * decay_step_);
        level_ = peak_ * remaining * remaining * remaining;
        if(position_ >= decay_samples_) {
            level_ = 0.0f;
            stage_ = Stage::Idle;
        }
    }
    return level_;
}

void ToyPiano::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    note_counter_ = 0;
    for(std::size_t i = 0; i < kTableSize; ++i)
        sine_[i] = std::sin(6.28318530718f * static_cast<float>(i) / kTableSize);
    sine_[kTableSize] = sine_[0];
    for(std::size_t note = 0; note < midi_increment_.size(); ++note)
        midi_increment_[note] = MidiToFrequency(static_cast<uint8_t>(note)) / sample_rate_;
    for(auto& voice : voices_)
        voice = Voice{};
    for(std::size_t i = 0; i < kNumKeys; ++i) {
        voices_[i].increment = voices_[i].base_increment = midi_increment_[kMidiNotes[i]];
        voices_[i].harmonic = kSecondHarmonic;
    }
}

void ToyPiano::Trigger(std::size_t key, float attack, float decay) {
    if(key < kNumKeys)
        voices_[key].envelope.Trigger(attack, decay, sample_rate_);
}

std::size_t ToyPiano::AllocateMidiVoice(uint8_t note) const {
    constexpr std::size_t kNone = kNumVoices;
    std::size_t idle = kNone, oldest = kNone;
    for(std::size_t i = kNumKeys; i < kNumVoices; ++i) {
        const Voice& voice = voices_[i];
        if(!voice.envelope.Active()) {
            if(idle == kNone)
                idle = i;
        } else if(voice.note == note) {
            return i;
        } else if(oldest == kNone
                  || note_counter_ - voice.order > note_counter_ - voices_[oldest].order) {
            oldest = i; // Unsigned difference stays correct when the counter wraps.
        }
    }
    return idle != kNone ? idle : oldest;
}

void ToyPiano::NoteOn(uint8_t note, uint8_t velocity, float attack, float decay) {
    if(note > 127 || velocity == 0 || velocity > 127)
        return;
    Voice& voice = voices_[AllocateMidiVoice(note)];
    voice.note = note;
    voice.order = ++note_counter_;
    // A stolen voice keeps its phase and level, so it changes pitch without a click.
    voice.increment = voice.base_increment = midi_increment_[note];
    voice.harmonic = DefaultHarmonic(voice.increment);
    voice.envelope.Trigger(attack, decay, sample_rate_, velocity / 127.0f);
}

int ToyPiano::MidiVoiceNote(std::size_t index) const {
    if(index >= kNumMidiVoices)
        return -1;
    const Voice& voice = voices_[kNumKeys + index];
    return voice.envelope.Active() ? voice.note : -1;
}

void ToyPiano::Modulate(std::size_t index, float pitch_ratio, float level, float harmonic, std::size_t ramp) {
    if(index >= kNumVoices)
        return;
    Voice& voice = voices_[index];
    voice.increment = voice.base_increment * pitch_ratio;
    // The anti-aliasing rule follows the modulated pitch.
    voice.harmonic = voice.increment < kMaxHarmonicIncrement ? harmonic : 0.0f;
    voice.level_target = level;
    if(!voice.envelope.Active()) { // silent: take the level at once, no stale ramp later
        voice.level = level;
        voice.level_ramp = 0;
        return;
    }
    voice.level_ramp = static_cast<uint32_t>(std::max<std::size_t>(1, ramp));
    voice.level_step = (level - voice.level) / static_cast<float>(voice.level_ramp);
}

void ToyPiano::ClearModulation(std::size_t ramp) {
    for(std::size_t i = 0; i < kNumVoices; ++i) {
        Voice& voice = voices_[i];
        Modulate(i, 1.0f, 1.0f, DefaultHarmonic(voice.base_increment), ramp);
    }
}

float ToyPiano::Sine(float phase) const {
    const float index = phase * kTableSize;
    const auto i = static_cast<std::size_t>(index);
    return sine_[i] + (sine_[i + 1] - sine_[i]) * (index - i);
}

float ToyPiano::ProcessRaw() {
    float sum = 0.0f;
    for(auto& voice : voices_) {
        if(!voice.envelope.Active())
            continue;
        float second_phase = 2.0f * voice.phase;
        if(second_phase >= 1.0f)
            second_phase -= 1.0f;
        const float wave = Sine(voice.phase) + voice.harmonic * Sine(second_phase);
        if(voice.level_ramp) { // linear level ramp: no zipper noise at the 1 kHz tick
            voice.level += voice.level_step;
            if(--voice.level_ramp == 0)
                voice.level = voice.level_target;
        }
        sum += wave * voice.envelope.Process() * voice.level; // level 1: exactly as in v0.2
        // Never reset phase on retrigger. No transcendental work per sample.
        voice.phase += voice.increment;
        if(voice.phase >= 1.0f)
            voice.phase -= 1.0f;
    }
    return sum * kVoiceGain;
}

// x / sqrt(1 + (x/c)^2): gain 1 and almost linear far below c, |y| < c for any x.
float ToyPiano::SoftLimit(float x) {
    const float r = x * (1.0f / kOutputCeiling);
    return x / std::sqrt(1.0f + r * r);
}

float ToyPiano::Process() { return SoftLimit(ProcessRaw()); }
}
