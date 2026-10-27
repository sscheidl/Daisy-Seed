#include "physical_modeling.h"
#include "stmlib/utils/random.h"
namespace physical_modeling {
void Synth::Init() {
    torus::InitResources();
    part_.Init();
#if PHYSICAL_MODELING_COMPOSED
    reverb_.Prepare(reverb_buffer_);
#else
    reverb_.Init(reverb_buffer_);
#endif
    write_ = read_ = overflow_ = 0; fade_ = 1; fading_out_ = false; chord_ = ChordSelector{};
}
bool Synth::NoteOn(unsigned note, unsigned velocity) {
    if(note > 127 || velocity > 127 || velocity == 0) return false;
    if(write_ - read_ == notes_.size()) { ++overflow_; return false; }
    notes_[write_++ % notes_.size()] = {uint8_t(note), uint8_t(velocity)};
    return true;
}
void Synth::Render(float* left, float* right) {
    parameters.Advance();
    const unsigned desired = parameters.DesiredModel();
    if(desired != ModelIndex()) fading_out_ = true;
    if(fading_out_ && fade_ == 0) {
        part_.set_model(static_cast<torus::ResonatorModel>(desired));
        fading_out_ = false;
    }
    // At most four triggers before each render; never overwrite an unrendered strike.
    if(!fading_out_) for(size_t i = 0; i < kVoices && read_ != write_; ++i) {
        const Note note = notes_[read_++ % notes_.size()];
        part_.Trigger(float(note.note), float(note.velocity) / 127.0f * parameters.Get(Excitation));
    }
    torus::Patch patch{};
    patch.structure = parameters.Get(Structure);
    patch.brightness = parameters.Get(Brightness);
    patch.damping = parameters.Get(Damping);
    patch.position = parameters.Get(Position);
    torus::PerformanceState state{};
    state.internal_exciter = true;
    state.tonic = 0;
    state.fm = parameters.PitchOffset();
    state.chord = chord_.Process(patch.structure);
#if PHYSICAL_MODELING_MOD
    if(voice_enabled_) {
        for(unsigned v = 0; v < kVoices; ++v) {
            const float p = voice_pressure_[v];
            voice_patch_[v].structure = Unit(patch.structure + voice_gain_[1] * p);
            voice_patch_[v].brightness = Unit(patch.brightness + voice_gain_[2] * p);
            voice_patch_[v].damping = Unit(patch.damping + voice_gain_[3] * p);
            voice_patch_[v].position = Unit(patch.position + voice_gain_[4] * p);
            voice_fm_[v] = 48.0f * voice_gain_[0] * p; // Frequency: 0..1 = 48 semitones
        }
        part_.set_voice_modulation(voice_patch_, voice_fm_);
    } else {
        part_.set_voice_modulation(nullptr, nullptr);
    }
#endif
    float input[kQuantum]{};
    part_.Process(state, patch, input, left, right, kQuantum);
    float wet_left[kQuantum], wet_right[kQuantum];
#if PHYSICAL_MODELING_COMPOSED
    reverb_.SetParameters(parameters);
#else
    // set_time is a feedback coefficient, not seconds. Keep strictly below unity.
    reverb_.set_time(0.35f + 0.63f * parameters.Get(Decay));
    reverb_.set_lp(0.05f + 0.9f * (1.0f - parameters.Get(ReverbDamping)));
    reverb_.set_diffusion(0.9f * parameters.Get(Diffusion));
    reverb_.set_input_gain(0.5f * parameters.Get(InputGain));
    reverb_.set_amount(1.0f); // Wet-only; external crossfade includes dry exactly once.
#endif
    for(size_t i = 0; i < kQuantum; ++i) {
        fade_ = fading_out_ ? std::max(0.0f, fade_ - 1.0f/192.0f) : std::min(1.0f, fade_ + 1.0f/192.0f);
        left[i] *= fade_; right[i] *= fade_;
        // Feed the reverb before model 6's dry polarity inversion: its L+R
        // input would otherwise cancel at Position=.5. Wet polarity is global.
        wet_left[i] = left[i]; wet_right[i] = right[i];
    }
    {
        torus::profile::Scope timing(torus::profile::Reverb);
        reverb_.Process(wet_left, wet_right, kQuantum);
    }
    torus::profile::Scope output_timing(torus::profile::Output);
#if PHYSICAL_MODELING_MOD
    // Level column (Mod on Daisy): exactly 1 with an empty matrix, so master * 1 is unchanged.
    const float level = parameters.Level();
    for(size_t i = 0; i < kQuantum; ++i) {
        left[i] = Output(Mix(left[i], wet_left[i], parameters.Get(Wet)) * level, parameters.Get(Master));
        const float dry_right = ModelIndex() == 5 ? -right[i] : right[i];
        right[i] = Output(Mix(dry_right, wet_right[i], parameters.Get(Wet)) * level, parameters.Get(Master));
    }
#else
    for(size_t i = 0; i < kQuantum; ++i) {
        left[i] = Output(Mix(left[i], wet_left[i], parameters.Get(Wet)), parameters.Get(Master));
        const float dry_right = ModelIndex() == 5 ? -right[i] : right[i];
        right[i] = Output(Mix(dry_right, wet_right[i], parameters.Get(Wet)), parameters.Get(Master));
    }
#endif
}
void Synth::Process(float* left, float* right, size_t size) {
    // Hardware provides exactly 48 samples; reject malformed callback lengths safely.
    if(size % kQuantum != 0 || size > kBlockSize) {
        std::fill_n(left, size, 0.0f); std::fill_n(right, size, 0.0f); return;
    }
    for(size_t i = 0; i < size; i += kQuantum) Render(left + i, right + i);
}
}
