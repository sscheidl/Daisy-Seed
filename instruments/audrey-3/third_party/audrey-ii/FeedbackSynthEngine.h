#pragma once
#ifndef IFS_FEEDBACK_SYNTH_ENGINE_H
#define IFS_FEEDBACK_SYNTH_ENGINE_H

#include <daisysp.h>
#include "BiquadFilters.h"
#include "DSPUtils.h"
#include "KarplusString.h"

namespace infrasonic {
namespace FeedbackSynth {

// Stages supplies the types Reverb (ReverbSc interface), Echo (EchoDelay interface) and
// Damper (Tone interface). Init() takes the reverb and echoes from the host, which owns
// their memory; they must be constructed first.
template <class Stages>
class Engine {

    public:
        Engine() = default;
        ~Engine() = default;

        using Reverb = typename Stages::Reverb;
        using Echo = typename Stages::Echo;

        void Init(const float sample_rate, Reverb *verb, Echo *echo_left, Echo *echo_right);

        void SetStringPitch(const float nn);

        void SetFeedbackGain(const float gain_dbfs);

        void SetFeedbackDelay(const float delay_s);
        void SetFeedbackLPFCutoff(const float cutoff_hz);
        void SetFeedbackHPFCutoff(const float cutoff_hz);

        void SetEchoDelayTime(const float echo_time);
        void SetEchoDelayFeedback(const float echo_fb);
        void SetEchoDelaySendAmount(const float echo_send);

        // Both range 0-1
        void SetReverbMix(const float mix);
        void SetReverbFeedback(const float time);

        void SetOutputLevel(const float level);

        // Audrey 3: the played note's gate, 0-1, before the echo send. At 1 the output
        // is exactly the Audrey II output. The feedback loop never stops.
        void SetGate(const float gate) { gate_target_ = gate; }

        void Process(float in, float &outL, float &outR);

    private:
        // long enough for 250ms at 48kHz
        static constexpr size_t kMaxFeedbackDelaySamp = 12000;

        float sample_rate_;
        float fb_gain_ = 0.0f;
        float echo_send_ = 0.0f;
        float verb_mix_ = 0.0f;
        float output_level_ = 0.5f;

        float gate_ = 1.0f;
        float gate_target_ = 1.0f;
        float gate_attack_coef_ = 1.0f;
        float gate_release_coef_ = 1.0f;

        float fb_delay_smooth_coef_;
        float fb_delay_samp_ = 1000.f;
        float fb_delay_samp_target_ = 64.f;

        infrasonic::KarplusString<typename Stages::Damper> strings_[2];
        daisysp::WhiteNoise noise_;
        daisysp::DelayLine<float, kMaxFeedbackDelaySamp> fb_delayline_[2];
        daisysp::Overdrive overdrive_[2];

        LPF12 fb_lpf_;
        HPF12 fb_hpf_;

        Reverb *verb_ = nullptr;
        Echo *echo_delay_[2] = {nullptr, nullptr};

        Engine(const Engine &other) = delete;
        Engine(Engine &&other) = delete;
        Engine& operator=(const Engine &other) = delete;
        Engine& operator=(Engine &&other) = delete;
};

using namespace daisysp;

template <class Stages>
void Engine<Stages>::Init(const float sample_rate, Reverb *verb, Echo *echo_left, Echo *echo_right) {
  echo_delay_[0] = echo_left;
  echo_delay_[1] = echo_right;
  verb_ = verb;

  sample_rate_ = sample_rate;
  fb_delay_smooth_coef_ = onepole_coef(0.2f, sample_rate);
  gate_attack_coef_ = onepole_coef(0.005f, sample_rate);
  gate_release_coef_ = onepole_coef(0.15f, sample_rate);

  noise_.Init();
  noise_.SetAmp(dbfs2lin(-90.0f));

  for (unsigned int i = 0; i < 2; i++) {

    strings_[i].Init(sample_rate);
    strings_[i].SetBrightness(0.98f);
    strings_[i].SetFreq(mtof(40.0f));
    strings_[i].SetDamping(0.4f);

    fb_delayline_[i].Init();

    echo_delay_[i]->Init(sample_rate);
    echo_delay_[i]->SetDelayTime(5.0f, true);
    echo_delay_[i]->SetFeedback(0.5f);
    echo_delay_[i]->SetLagTime(0.5f);

    overdrive_[i].Init();
    overdrive_[i].SetDrive(0.4);
  }

  verb_->Init(sample_rate);
  verb_->SetFeedback(0.85f);
  verb_->SetLpFreq(12000.0f);

  fb_lpf_.Init(sample_rate);
  fb_lpf_.SetQ(0.9f);
  fb_lpf_.SetCutoff(18000.0f);

  fb_hpf_.Init(sample_rate);
  fb_hpf_.SetQ(0.9f);
  fb_hpf_.SetCutoff(60.f);
}

template <class Stages>
void Engine<Stages>::SetStringPitch(const float nn) {
  const auto freq = mtof(nn);
  strings_[0].SetFreq(freq);
  strings_[1].SetFreq(freq);
}

template <class Stages>
void Engine<Stages>::SetFeedbackGain(const float gain_db) {
  fb_gain_ = dbfs2lin(gain_db);
}

template <class Stages>
void Engine<Stages>::SetFeedbackDelay(const float delay_s) {
  fb_delay_samp_target_ =
      DSY_CLAMP(delay_s * sample_rate_, 1.0f,
                static_cast<float>(kMaxFeedbackDelaySamp - 1));
}

template <class Stages>
void Engine<Stages>::SetFeedbackLPFCutoff(const float cutoff_hz) {
  fb_lpf_.SetCutoff(cutoff_hz);
}

template <class Stages>
void Engine<Stages>::SetFeedbackHPFCutoff(const float cutoff_hz) {
  fb_hpf_.SetCutoff(cutoff_hz);
}

template <class Stages>
void Engine<Stages>::SetEchoDelayTime(const float echo_time) {
  echo_delay_[0]->SetDelayTime(echo_time);
  echo_delay_[1]->SetDelayTime(echo_time);
}

template <class Stages>
void Engine<Stages>::SetEchoDelayFeedback(const float echo_fb) {
  echo_delay_[0]->SetFeedback(echo_fb);
  echo_delay_[1]->SetFeedback(echo_fb);
}

template <class Stages>
void Engine<Stages>::SetEchoDelaySendAmount(const float echo_send) {
  echo_send_ = echo_send;
}

template <class Stages>
void Engine<Stages>::SetReverbMix(const float mix) {
  verb_mix_ = fclamp(mix, 0.0f, 1.0f);
}

template <class Stages>
void Engine<Stages>::SetReverbFeedback(const float time) { verb_->SetFeedback(time); }

template <class Stages>
void Engine<Stages>::SetOutputLevel(const float level) { output_level_ = level; }

template <class Stages>
void Engine<Stages>::Process(float in, float &outL, float &outR) {
  // --- Update audio-rate-smoothed control params ---

  fonepole(fb_delay_samp_, fb_delay_samp_target_, fb_delay_smooth_coef_);

  // --- Process Samples ---

  float inL, inR, sampL, sampR, echoL, echoR, verbL, verbR;
  const float noise_samp = noise_.Process();

  // ---> Feedback Loop

  // Get noise + feedback output
  inL = fb_delayline_[0].Read(fb_delay_samp_) + noise_samp + in;
  inR = fb_delayline_[1].Read(daisysp::fmax(1.0f, fb_delay_samp_ - 4.f)) +
        noise_samp + in;

  // Process through KS resonator
  sampL = strings_[0].Process(inL);
  sampR = strings_[1].Process(inR);

  // Distort + Clip
  sampL = overdrive_[0].Process(sampL);
  sampR = overdrive_[1].Process(sampR);

  // Filter in feedback loop
  fb_lpf_.ProcessStereo(sampL, sampR);
  fb_hpf_.ProcessStereo(sampL, sampR);

  // ---> Reverb

  verb_->Process(sampL, sampR, &verbL, &verbR);

  //       (sampL * (1.0f - verb_mix_)) + verbL * verb_mix_;
  //       sampL - sampL * verb_mix + verbL * verb_mix_;
  sampL -= (sampL - verbL) * verb_mix_;
  sampR -= (sampR - verbR) * verb_mix_;

  // ---> Resonator feedback

  // Write back into delay with attenuation
  fb_delayline_[0].Write(sampL * fb_gain_);
  fb_delayline_[1].Write(sampR * fb_gain_);

  // ---> Note gate (Audrey 3)

  fonepole(gate_, gate_target_,
           gate_target_ > gate_ ? gate_attack_coef_ : gate_release_coef_);
  sampL *= gate_;
  sampR *= gate_;

  // ---> Echo Delay

  echoL = echo_delay_[0]->Process(sampL * echo_send_);
  echoR = echo_delay_[1]->Process(sampR * echo_send_);

  sampL = 0.5f * (sampL + echoL);
  sampR = 0.5f * (sampR + echoR);

  // ---> Output
  outL = sampL * output_level_;
  outR = sampR * output_level_;
}

}
}

#endif
