#pragma once
#include "parameter_manager.h"
#include "audio_output.h"
#include "dsp/part.h"
namespace physical_modeling {
class Synth {
  public:
    void Init();
    bool NoteOn(unsigned note, unsigned velocity);
    void Process(float* left, float* right, size_t size);
    ParameterManager parameters;
    unsigned ModelIndex() const { return unsigned(part_.model()); }
    uint32_t TriggerCount() const { return part_.triggers(); }
    float VoiceNote(unsigned voice) const { return part_.voice_note(voice); }
    size_t PendingNotes() const { return write_ - read_; }
    uint32_t OverflowCount() const { return overflow_; }
#if PHYSICAL_MODELING_MOD
    // Mod on Daisy per-voice path (Poly AT). gain: normalized depth per voice target
    // Frequency, Structure, Brightness, Damping, Position; pressure: smoothed, per voice.
    // enabled == false restores the shared upstream patch path.
    void SetVoiceModulation(bool enabled, const std::array<float, 5>& gain,
                            const std::array<float, kVoices>& pressure) {
        voice_enabled_ = enabled;
        voice_gain_ = gain;
        voice_pressure_ = pressure;
    }
    const torus::Patch& VoicePatch(unsigned voice) const { return voice_patch_[voice]; }
    float VoiceFm(unsigned voice) const { return voice_fm_[voice]; }
#endif
  private:
    struct Note { uint8_t note, velocity; };
    void Render(float* left, float* right);
    torus::Part part_{};
    torus::Reverb reverb_{};
    uint16_t reverb_buffer_[kReverbWords]{};
    std::array<Note, 256> notes_{};
    uint32_t write_ = 0, read_ = 0, overflow_ = 0;
    float fade_ = 1;
    bool fading_out_ = false;
    ChordSelector chord_{};
#if PHYSICAL_MODELING_MOD
    bool voice_enabled_ = false;
    std::array<float, 5> voice_gain_{};
    std::array<float, kVoices> voice_pressure_{};
    torus::Patch voice_patch_[kVoices]{};
    float voice_fm_[kVoices]{};
#endif
};
}
