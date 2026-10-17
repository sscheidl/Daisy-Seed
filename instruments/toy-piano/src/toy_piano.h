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
    // The attack ramps from the current level to `peak`; the decay starts at `peak`.
    void Trigger(float attack, float decay, float sample_rate, float peak = 1.0f);
    float Process();
    float Level() const { return level_; }
    bool Active() const { return stage_ != Stage::Idle; }
  private:
    enum class Stage { Idle, Attack, Decay };
    Stage stage_ = Stage::Idle;
    uint32_t position_ = 0, attack_samples_ = 1, decay_samples_ = 1;
    float attack_step_ = 1.0f, decay_step_ = 1.0f;
    float start_level_ = 0.0f, level_ = 0.0f, peak_ = 1.0f;
};

class ToyPiano {
  public:
    void Init(float sample_rate = kSampleRate);
    // Voices 0..15: one fixed voice per key.
    void Trigger(std::size_t key, float attack, float decay);
    // Voices 16..31: free pool for MIDI. Re-striking a sounding note reuses its
    // voice; otherwise an idle voice, then the oldest one, is taken.
    void NoteOn(uint8_t note, uint8_t velocity, float attack, float decay);
    // Sum of all voices, linear. |result| can exceed kOutputCeiling.
    float ProcessRaw();
    // ProcessRaw() through a soft limiter: always below kOutputCeiling.
    float Process();
    static float SoftLimit(float x);
    const AdEnvelope& Envelope(std::size_t key) const { return voices_[key].envelope; }
    const AdEnvelope& MidiEnvelope(std::size_t index) const { return voices_[kNumKeys + index].envelope; }
    // Note of MIDI voice `index` (0..15), or -1 while it is silent.
    int MidiVoiceNote(std::size_t index) const;
  private:
    static constexpr std::size_t kTableSize = 2048;
    struct Voice {
        AdEnvelope envelope;
        float phase = 0.0f;
        float increment = 0.0f;
        float harmonic = 0.0f;
        int note = -1;
        uint32_t order = 0;
    };
    float Sine(float phase) const;
    std::size_t AllocateMidiVoice(uint8_t note) const;
    float sample_rate_ = kSampleRate;
    uint32_t note_counter_ = 0;
    std::array<float, kTableSize + 1> sine_{};
    std::array<float, 128> midi_increment_{};
    std::array<Voice, kNumVoices> voices_{};
};
}
