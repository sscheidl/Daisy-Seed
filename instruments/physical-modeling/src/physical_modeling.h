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
};
}
