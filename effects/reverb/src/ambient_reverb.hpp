#pragma once
// SPDX-License-Identifier: MIT
#include "primitives.hpp"
namespace ambient {
enum class Mode : unsigned { Cloud, Bloom, Abyss, Halo, Scatter };
struct Parameters {
    Mode mode = Mode::Cloud;
    Shape shape = Shape::Sine;
    float decaySeconds = 14, toneHz = 5500, lowCutHz = 65, preDelayMs = 25;
    float modRateHz = .18f, modDepthMs = .8f, width = 1, mix = .45f, diffusion = .85f;
    float texture = .4f, riseSeconds = 1, outputGain = .8f;
    bool hold = false, bypass = false, trails = true;
};
// Plain storage, deliberately no constructors/default member initializers.
// Place in SDRAM and call Prepare AFTER the hardware initializes SDRAM.
struct Memory {
    std::array<std::array<float, 16384>, 8> tank;
    std::array<std::array<float, 4096>, 8> tankAllpass;
    std::array<std::array<float, 2048>, 8> inputAllpass;
    std::array<std::array<float, 32768>, 2> preDelay;
    std::array<std::array<float, 8192>, 4> pitch;
};
struct Diagnostics {
    uint64_t samples = 0, guardEvents = 0;
    float peakWet = 0;
};
class AmbientReverb {
  public:
    bool Prepare(float sampleRate, Memory &memory);
    // Single audio-thread ownership. No locks or allocations; copy a GUI snapshot at block
    // boundaries.
    void SetParameters(const Parameters &parameters, bool immediate = false);
    Stereo Process(Stereo input);
    void ProcessBlock(const float *left, const float *right, float *outLeft, float *outRight,
                      std::size_t frames);
    const Parameters &GetParameters() const {
        return parameters_;
    }
    const Diagnostics &GetDiagnostics() const {
        return diagnostics_;
    }
    bool IsPrepared() const {
        return ready_;
    }
    float SampleRate() const {
        return sampleRate_;
    }
    static Parameters Preset(Mode mode);

  private:
    struct State {
        Ramp gain[8], mode[5];
        Ramp tone, highpass, preDelay, depth, rate, width, dry, wet, diffusion, texture, riseStep,
            level, send, hold;
    } state_;
    bool ready_ = false;
    float sampleRate_ = 48000, waveSmoothing_ = 0, envelopeRelease_ = 0;
    Parameters parameters_{};
    Diagnostics diagnostics_{};
    Delay<16384> tank_[8];
    Delay<4096> tankAp_[8];
    Delay<2048> inputAp_[8];
    Delay<32768> pre_[2];
    unsigned tankLength_[8]{}, apLength_[8]{}, inputLength_[8]{};
    Lowpass damping_[8];
    Highpass highpass_[8];
    Oscillator lfo_[8];
    Octave up_, down_;
    Stereo feedback_{};
    float envelope_ = 0, swell_ = 1;
    bool gate_ = false;
    float Guard(float x);
};
} // namespace ambient
