#pragma once
// SPDX-License-Identifier: MIT
// 48 kHz host adapter for the FV-1 interpreter: the program runs at 32 kHz (nearest SAI-friendly rate
// to the FV-1 design rate of 32 768 Hz) between two 3:2 polyphase FIR resamplers, so delay times,
// LFO rates and filter corners keep their 32 kHz values inside a 48 kHz product (Toy Piano).
//
// Contract: seed::audio FX interface 1.0 at 48 kHz. Prepare({48000, max_block}, storage) with
// max_block a multiple of 3 (48 -> 32 VM samples); Process is wet-only, bounded and allocation-free.
// Filter: windowed sinc at 96 kHz, 96 taps, Kaiser beta 7, cutoff 14.5 kHz (passband to about
// 12 kHz, alias/image rejection measured in tests/test_fx48.cpp).
#include "fv1_vm.h"
#include <cmath>

namespace seed::fx::fv1 {

class Polyphase {
  public:
    static constexpr unsigned kTaps = 96;
    // Up-by-L, FIR, down-by-M; gain L restores the level lost by zero insertion.
    void Init(unsigned up, unsigned down) {
        l_ = up; m_ = down; // L >= 2 (the history holds kTaps / 2 inputs)
        constexpr double kFs = 96000, kCut = 14500, kBeta = 7;
        constexpr double kPi = 3.14159265358979323846;
        auto i0 = [](double x) {
            double sum = 1, term = 1;
            for(int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
            return sum;
        };
        const double centre = (kTaps - 1) / 2.0;
        double dc = 0;
        for(unsigned k = 0; k < kTaps; ++k) {
            const double t = k - centre, r = t / centre;
            const double sinc = t == 0 ? 2 * kCut / kFs : std::sin(2 * kPi * kCut * t / kFs) / (kPi * t);
            h_[k] = float(sinc * i0(kBeta * std::sqrt(std::fmax(0.0, 1 - r * r))) / i0(kBeta));
            dc += h_[k];
        }
        for(auto& c : h_) c = float(c * double(up) / dc); // exact passband gain L at DC
        Reset();
    }
    void Reset() {
        for(auto& x : hist_) x = 0;
        pos_ = 0; phase_ = 0;
    }
    // Consumes `in` input samples and writes in * L / M outputs (the caller keeps blocks exact).
    unsigned Process(const float* in, unsigned n, float* out) {
        unsigned produced = 0;
        for(unsigned i = 0; i < n; ++i) {
            pos_ = pos_ + 1 == kHist ? 0 : pos_ + 1;
            hist_[pos_] = hist_[pos_ + kHist] = in[i]; // mirrored: reads run backwards without wrap
            // Outputs whose 96 kHz index falls on this input's L slots.
            while(phase_ < l_) {
                float acc = 0;
                const float* x = hist_ + pos_ + kHist;
                for(unsigned k = phase_; k < kTaps; k += l_) acc += h_[k] * *x--;
                out[produced++] = acc;
                phase_ += m_;
            }
            phase_ -= l_;
        }
        return produced;
    }
  private:
    static constexpr unsigned kHist = kTaps / 2; // >= ceil(kTaps / L) inputs for L >= 2
    float h_[kTaps] = {};
    float hist_[2 * kHist] = {};
    unsigned l_ = 1, m_ = 1, pos_ = 0, phase_ = 0;
};

template <typename T> class Fx48 {
  public:
    static constexpr unsigned kHostRate = 48000, kVmRate = 32000;
    static constexpr std::size_t kMaxBlock = 48;

    bool Prepare(const seed::audio::Config& config, Storage<T>& storage) {
        if(config.sample_rate != kHostRate || config.max_block == 0 || config.max_block > kMaxBlock ||
           config.max_block % 3 != 0)
            return false;
        config_ = config;
        for(auto& d : down_) d.Init(2, 3);
        for(auto& u : up_) u.Init(3, 2);
        return vm_.Prepare({kVmRate, kMaxBlock * 2 / 3}, storage);
    }
    void LoadProgram(const uint32_t* words) { vm_.LoadProgram(words); }
    // Clears the VM state, its delay RAM and the resampler histories (not real-time cheap).
    void Reset() {
        vm_.Reset();
        for(auto& d : down_) d.Reset();
        for(auto& u : up_) u.Reset();
    }
    void SetParameters(const Parameters& p) { vm_.SetParameters(p); }
    // Wet only. Frames must be a multiple of 3 so that each block maps to whole 32 kHz samples.
    bool Process(const seed::audio::StereoBlock& b) {
        if(!seed::audio::Valid(config_, b) || b.frames % 3 != 0) return false;
        const unsigned n = unsigned(b.frames), v = n * 2 / 3;
        float il[kMaxBlock], ir[kMaxBlock], ol[kMaxBlock], orr[kMaxBlock];
        if(down_[0].Process(b.left, n, il) != v || down_[1].Process(b.right, n, ir) != v) return false;
        if(!vm_.Process({il, ir, ol, orr, v})) return false;
        return up_[0].Process(ol, v, b.out_left) == n && up_[1].Process(orr, v, b.out_right) == n;
    }
    const Vm<T>& Interpreter() const { return vm_; }
  private:
    seed::audio::Config config_{};
    Vm<T> vm_;
    Polyphase down_[2], up_[2];
};

} // namespace seed::fx::fv1
