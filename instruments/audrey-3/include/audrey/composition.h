#pragma once
// SPDX-License-Identifier: MIT
// The Audrey 3 product composition: which stages the feedback engine runs with, and how
// parameter values reach it. Replacing the reverb means changing `Reverb` here only.
#include "EchoDelay.h"
#include "FeedbackSynthEngine.h"
#include "audrey/params.h"
#include <Effects/reverbsc.h> // DaisySP-LGPL
#include <Filters/tone.h>     // DaisySP-LGPL

namespace audrey {

struct Stages {
    using Reverb = daisysp::ReverbSc;
    using Echo = infrasonic::EchoDelay<48000 * 5>; // 5 s at 48 kHz
    using Damper = daisysp::Tone;
};
using Engine = infrasonic::FeedbackSynth::Engine<Stages>;

template <class EngineType> class EngineSink {
  public:
    explicit EngineSink(EngineType& engine) : engine_(engine) {}
    void Apply(uint8_t id, float value) {
        switch(id) {
            case kFrequency: engine_.SetStringPitch(value); break;
            case kFeedbackGain: engine_.SetFeedbackGain(value); break;
            case kFeedbackBody: engine_.SetFeedbackDelay(value); break;
            case kFeedbackLpf: engine_.SetFeedbackLPFCutoff(value); break;
            case kFeedbackHpf: engine_.SetFeedbackHPFCutoff(value); break;
            case kReverbMix: engine_.SetReverbMix(value); break;
            case kReverbDecay: engine_.SetReverbFeedback(value); break;
            case kEchoSend: engine_.SetEchoDelaySendAmount(value); break;
            case kEchoTime: engine_.SetEchoDelayTime(value); break;
            case kEchoFeedback: engine_.SetEchoDelayFeedback(value); break;
            case kOutputVolume: engine_.SetOutputLevel(value); break;
            default: break;
        }
    }

  private:
    EngineType& engine_;
};

} // namespace audrey
