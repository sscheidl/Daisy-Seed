#pragma once
// SPDX-License-Identifier: MIT
#include "primitives.hpp"
namespace ambient {
#if defined(AMBIENT_PITCH_GATE) || defined(AMBIENT_WET_MAX_HZ)
#error "Ambient 0.4 uses one shared pitch/32 kHz policy; remove obsolete experiment defines"
#endif
// Append modes to preserve existing numeric IDs.
enum class Mode : unsigned { Cloud, Bloom, Abyss, Halo, Scatter, Shimmer };
struct Parameters {
    Mode mode = Mode::Cloud;
    Shape shape = Shape::Sine;
    float decaySeconds = 14, toneHz = 5500, lowCutHz = 65, preDelayMs = 25;
    float modRateHz = .18f, modDepthMs = .8f, width = 1, mix = .45f, diffusion = .85f;
    float texture = .4f, riseSeconds = 1, outputGain = .8f;
    // Applied by the firmware wet-path adapter; the standalone engine ignores these.
    float pan = 0, stretch = 0;
    bool hold = false, bypass = false, trails = true, sustain = false;
};
// Diagnostic layout (default off): capacities sized for a core up to 48 kHz
// (WetPath prepares at 32 kHz; Stretch skips core calls), with the
// all-pass and pitch buffers split off so a host can place them in internal RAM.
// Reads stay within each line's length, so the output is bit-identical.
#ifndef AMBIENT_COMPACT_48K
#define AMBIENT_COMPACT_48K 0
#endif
#if AMBIENT_COMPACT_48K
constexpr std::size_t kTankCapacity = 8192, kTankAllpassCapacity = 2048,
                      kInputAllpassCapacity = 1024, kPreDelayCapacity = 16384;
#else
constexpr std::size_t kTankCapacity = 16384, kTankAllpassCapacity = 4096,
                      kInputAllpassCapacity = 2048, kPreDelayCapacity = 32768;
#endif
constexpr std::size_t kPitchCapacity = 8192;
#if AMBIENT_COMPACT_48K
struct FastMemory {
    std::array<std::array<float, kTankAllpassCapacity>, 8> tankAllpass;
    std::array<std::array<float, kInputAllpassCapacity>, 8> inputAllpass;
    std::array<std::array<float, kPitchCapacity>, 4> pitch;
};
#endif
// Plain storage, deliberately no constructors/default member initializers.
// Place in SDRAM and call Prepare AFTER the hardware initializes SDRAM.
struct Memory {
    std::array<std::array<float, kTankCapacity>, 8> tank;
#if AMBIENT_COMPACT_48K
    std::array<std::array<float, kPreDelayCapacity>, 2> preDelay;
    FastMemory *fast; // Set by the host before Prepare.
#else
    std::array<std::array<float, kTankAllpassCapacity>, 8> tankAllpass;
    std::array<std::array<float, kInputAllpassCapacity>, 8> inputAllpass;
    std::array<std::array<float, kPreDelayCapacity>, 2> preDelay;
    std::array<std::array<float, kPitchCapacity>, 4> pitch;
#endif
};
struct Diagnostics {
    uint64_t samples = 0, guardEvents = 0;
    uint64_t upSamples = 0, downSamples = 0;
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
        Ramp gain[8], mode[6];
        Ramp tone, highpass, preDelay, depth, rate, width, dry, wet, diffusion, texture, riseStep,
            level, send, hold, sustain;
    } state_;
    bool ready_ = false;
    float sampleRate_ = 48000, waveSmoothing_ = 0, envelopeRelease_ = 0;
    Parameters parameters_{};
    Diagnostics diagnostics_{};
    Delay<kTankCapacity> tank_[8];
    Delay<kTankAllpassCapacity> tankAp_[8];
    Delay<kInputAllpassCapacity> inputAp_[8];
    Delay<kPreDelayCapacity> pre_[2];
    unsigned tankLength_[8]{}, apLength_[8]{}, inputLength_[8]{};
    Lowpass damping_[8];
    Highpass highpass_[8];
    Oscillator lfo_[8];
    Octave up_, down_;
    bool upRunning_ = true, downRunning_ = true;
    Stereo feedback_{};
    float envelope_ = 0, swell_ = 1;
    bool gate_ = false;
    float Guard(float x);
};
} // namespace ambient
