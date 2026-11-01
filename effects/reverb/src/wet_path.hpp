#pragma once
// SPDX-License-Identifier: MIT
#include "ambient_reverb.hpp"

#ifndef AMBIENT_CONTIGUOUS_FIR
#define AMBIENT_CONTIGUOUS_FIR 0
#endif

namespace ambient {
// Shared wet-clock policy for every host, independent of the codec sample rate.
constexpr float kMinimumWetRate = 12000.f;
constexpr float kMaximumWetRate = 32000.f;
// Keeps dry audio at codec rate. The wet engine is clocked at 12–32 kHz by a
// fractional scheduler; a 64-tap low-pass FIR precedes decimation and the wet
// output is linearly interpolated. No allocation or coefficient design in Process().
class WetPath {
  public:
    bool Prepare(float sampleRate, Memory &memory);
    void SetParameters(const Parameters &parameters);
    void ServiceFilter(); // Call at control rate, never once per sample.
    void SetLegacyMono(bool enabled);
    // Embedded FX hosts own the dry mix. Exact endpoints avoid cos(pi/2) leakage.
    void ForceWetOnly() { dry_.Set(0.f, 0); wet_.Set(1.f, 0); }
    Stereo Process(Stereo input, bool rightConnected);
    const Diagnostics &GetDiagnostics() const { return core_.GetDiagnostics(); }
    float InternalRateRatio() const { return ratio_; }
    float InternalSampleRate() const { return sampleRate_ * ratio_; }

  private:
    static constexpr unsigned kTaps = 64;
    static constexpr unsigned kFilterBands = 17;
    static_assert((kTaps & (kTaps - 1)) == 0, "FIR ring size must be a power of two");
    AmbientReverb core_;
    Parameters parameters_{};
    float sampleRate_ = 48000, ratio_ = 1, targetRatio_ = 1, phase_ = 0;
    float minimumRatio_ = .25f, maximumRatio_ = 2.f / 3.f;
    float filterRatio_ = -1;
#if AMBIENT_CONTIGUOUS_FIR
    // Diagnostic (default off): each sample is stored twice, so the newest kTaps
    // samples are contiguous; the summation order is unchanged (bit-identical).
    std::array<float, kTaps> coefficients_{};
    std::array<float, 2 * kTaps> historyLeft_{}, historyRight_{};
#else
    std::array<float, kTaps> coefficients_{}, historyLeft_{}, historyRight_{};
#endif
    std::array<std::array<float, kTaps>, kFilterBands> coefficientBank_{};
    unsigned historyIndex_ = 0;
    Stereo previousWet_{}, currentWet_{};
    Ramp dry_, wet_, send_, panLeft_, panRight_, mono_;
    bool ready_ = false;
    void DesignFilterBank(); // Called only by Prepare(), before audio starts.
    void ConfigureFilter(float safeRatio);
    Stereo Filter(Stereo input);
    float StretchRatio(float stretch) const;
};
} // namespace ambient
