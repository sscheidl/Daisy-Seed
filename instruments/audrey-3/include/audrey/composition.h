#pragma once
// SPDX-License-Identifier: MIT
// The Audrey 3 product composition: which stages the feedback engine runs with, and how
// parameter values reach them. Replacing a stage means changing `Stages` here only.
#include "FeedbackSynthEngine.h"
#include "audrey/params.h"
#include "echo_delay/echo.h"
#include "sc_reverb/reverb.h"
#include <Filters/tone.h> // DaisySP-LGPL

namespace audrey {

// Adapter for a stage inside the feedback loop. The reverb's output returns to the string a
// few samples later, so the engine cannot work in blocks. It hands the FX contract a block of
// one frame: any effect under FX contract 1.0 fits, unchanged.
template <class Fx> class FrameStage : public Fx {
  public:
    using Fx::Process;
    void Process(float left, float right, float& out_left, float& out_right) {
        Fx::Process(seed::audio::StereoBlock{&left, &right, &out_left, &out_right, 1});
    }
};

struct Stages {
    using Reverb = FrameStage<seed::fx::ScReverb>;
    using Echo = FrameStage<seed::fx::StereoEcho>;
    using Damper = daisysp::Tone;
};
using Engine = infrasonic::FeedbackSynth::Engine<Stages>;

// Parameter mapping of this product. The engine gets what it routes and mixes, the effects
// get their own parameters directly.
template <class StagesType> class EngineSink {
  public:
    using EngineType = infrasonic::FeedbackSynth::Engine<StagesType>;
    using Reverb = typename StagesType::Reverb;
    using Echo = typename StagesType::Echo;

    EngineSink(EngineType& engine, Reverb& reverb, Echo& echo) : engine_(engine), reverb_(reverb), echo_(echo) {}

    void Apply(uint8_t id, float value) {
        switch(id) {
            case kFrequency: engine_.SetStringPitch(value); break;
            case kFeedbackGain: engine_.SetFeedbackGain(value); break;
            case kFeedbackBody: engine_.SetFeedbackDelay(value); break;
            case kFeedbackLpf: engine_.SetFeedbackLPFCutoff(value); break;
            case kFeedbackHpf: engine_.SetFeedbackHPFCutoff(value); break;
            case kReverbMix: engine_.SetReverbMix(value); break;
            case kReverbDecay: {
                auto p = reverb_.Requested();
                p.feedback = value;
                reverb_.SetParameters(p);
                break;
            }
            case kEchoSend: engine_.SetEchoDelaySendAmount(value); break;
            case kEchoTime: {
                auto p = echo_.Requested();
                p.time_s = value;
                echo_.SetParameters(p);
                break;
            }
            case kEchoFeedback: {
                auto p = echo_.Requested();
                p.feedback = value;
                echo_.SetParameters(p);
                break;
            }
            case kOutputVolume: engine_.SetOutputLevel(value); break;
            default: break;
        }
    }

  private:
    EngineType& engine_;
    Reverb& reverb_;
    Echo& echo_;
};

} // namespace audrey
