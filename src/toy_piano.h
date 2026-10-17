#pragma once
#include "config.h"

namespace toy_piano {
float MidiToFrequency(uint8_t note);
float LogTime(float position, float minimum, float maximum);

// Additional 20 ms smoothing + hysteresis, after libDaisy's AnalogControl.
class PotFilter {
  public:
    float Process(float input);
  private:
    float smoothed_ = 0.0f;
    float stable_ = 0.0f;
    bool initialized_ = false;
};

class AdEnvelope {
  public:
    void Trigger(float attack, float decay, float sample_rate);
    float Process();
    float Level() const { return level_; }
    bool Active() const { return stage_ != Stage::Idle; }
  private:
    enum class Stage { Idle, Attack, Decay };
    Stage stage_ = Stage::Idle;
    uint32_t position_ = 0, attack_samples_ = 1, decay_samples_ = 1;
    float attack_step_ = 1.0f, decay_step_ = 1.0f;
    float start_level_ = 0.0f, level_ = 0.0f;
};

class ToyPiano {
  public:
    void Init(float sample_rate = kSampleRate);
    void Trigger(std::size_t key, float attack, float decay);
    float Process();
    const AdEnvelope& Envelope(std::size_t key) const { return voices_[key].envelope; }
  private:
    static constexpr std::size_t kTableSize = 2048;
    struct Voice {
        AdEnvelope envelope;
        float phase = 0.0f;
        float increment = 0.0f;
    };
    float Sine(float phase) const;
    float sample_rate_ = kSampleRate;
    std::array<float, kTableSize + 1> sine_{};
    std::array<Voice, kNumKeys> voices_{};
};
}
