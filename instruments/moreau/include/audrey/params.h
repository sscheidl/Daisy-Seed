#pragma once
// SPDX-License-Identifier: MIT
// The eleven Audrey parameters: one table for pots and MIDI CC, with the ranges, curves
// and smoothing of the Audrey II firmware (Source/FeedbackSynthControls.cpp upstream).
// Runs in the audio callback; no allocation.
#include "DSPUtils.h"
#include <Utility/dsp.h>
#include <cstdint>

namespace audrey {

enum Param : uint8_t {
    kFrequency,
    kFeedbackGain,
    kFeedbackBody,
    kFeedbackLpf,
    kFeedbackHpf,
    kReverbMix,
    kReverbDecay,
    kEchoSend,
    kEchoTime,
    kEchoFeedback,
    kOutputVolume,
    kParamCount
};

struct ParamSpec {
    const char* name;
    float initial, min, max;
    float smooth_t60; // seconds
    daisysp::Mapping mapping;
    float initial_norm; // position of `initial` on the 0..1 control
};

constexpr uint8_t kFirstParamCc = 20; // CC 20..30 follow the Param order
constexpr float kReverbDecayTension = -3.f;

inline const ParamSpec& Spec(uint8_t id) {
    using M = daisysp::Mapping;
    static const ParamSpec specs[kParamCount] = {
        {"Frequency", 40.f, 16.f, 72.f, 0.2f, M::LINEAR, 24.f / 56.f},
        {"Feedback Gain", -60.f, -60.f, 12.f, 0.05f, M::LINEAR, 0.f},
        {"Feedback Body", 0.001f, 0.001f, 0.1f, 1.f, M::EXP, 0.f},
        {"Feedback LPF", 18000.f, 100.f, 18000.f, 0.05f, M::LOG, 1.f},
        {"Feedback HPF", 250.f, 10.f, 4000.f, 0.05f, M::LOG, 0.5372f},
        {"Reverb Mix", 0.f, 0.f, 1.f, 0.05f, M::LINEAR, 0.f},
        {"Reverb Decay", 0.2f, 0.2f, 1.f, 0.05f, M::LINEAR, 0.f},
        {"Echo Send", 0.f, 0.f, 1.f, 0.05f, M::EXP, 0.f},
        {"Echo Time", 0.5f, 0.05f, 5.f, 0.1f, M::EXP, 0.30151f},
        {"Echo Feedback", 0.f, 0.f, 1.5f, 0.05f, M::LINEAR, 0.f},
        {"Output Volume", 0.5f, 0.f, 1.f, 0.05f, M::EXP, 0.70711f},
    };
    return specs[id];
}

// Targets and smoothed values. Apply() in the sink only sees real changes: the filter
// coefficients cost a tan() each, and an unchanged value gives the same result.
class ParamBank {
  public:
    void Init(float control_rate) {
        for(uint8_t i = 0; i < kParamCount; ++i) {
            const ParamSpec& s = Spec(i);
            coef_[i] = infrasonic::onepole_coef_t60(s.smooth_t60, control_rate);
            norm_[i] = s.initial_norm;
            target_[i] = value_[i] = s.initial;
        }
        warp_ = false;
        started_ = false;
    }

    // A control position, 0..1. Same order as upstream: curve, then range.
    void SetNorm(uint8_t id, float norm) {
        norm_[id] = daisysp::fclamp(norm, 0.f, 1.f);
        target_[id] = Target(id);
    }

    // A played note: the full MIDI range, beyond the range of the control.
    void SetNote(uint8_t note, bool immediate) {
        const ParamSpec& s = Spec(kFrequency);
        norm_[kFrequency] = daisysp::fclamp((float(note) - s.min) / (s.max - s.min), 0.f, 1.f);
        target_[kFrequency] = float(note);
        if(immediate)
            value_[kFrequency] = target_[kFrequency];
    }

    // The Audrey II delay switch: halves the control value, not the time (about 1/4 time).
    void SetWarp(bool on) {
        warp_ = on;
        target_[kEchoTime] = Target(kEchoTime);
    }

    float Norm(uint8_t id) const { return norm_[id]; }
    float Target(uint8_t id) const {
        const ParamSpec& s = Spec(id);
        float x = norm_[id];
        if(id == kReverbDecay)
            x = infrasonic::ftension(x, kReverbDecayTension);
        if(id == kEchoTime && warp_)
            x *= 0.5f;
        return daisysp::fmap(x, s.min, s.max, s.mapping);
    }
    float TargetValue(uint8_t id) const { return target_[id]; }
    float Value(uint8_t id) const { return value_[id]; }
    bool Warp() const { return warp_; }

    // Once per audio block. The first call jumps to the targets, as upstream does.
    template <class Sink> void Process(Sink& sink) {
        for(uint8_t i = 0; i < kParamCount; ++i) {
            if(!started_)
                value_[i] = target_[i];
            else
                daisysp::fonepole(value_[i], target_[i], coef_[i]);
            if(!started_ || value_[i] != applied_[i]) {
                applied_[i] = value_[i];
                sink.Apply(i, value_[i]);
            }
        }
        started_ = true;
    }

  private:
    float coef_[kParamCount] = {}, norm_[kParamCount] = {}, target_[kParamCount] = {};
    float value_[kParamCount] = {}, applied_[kParamCount] = {};
    bool warp_ = false, started_ = false;
};

} // namespace audrey
