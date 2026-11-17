#pragma once
// SPDX-License-Identifier: MIT
// Host-side composition shared by the standalone Cumulonimbus firmware and Toy Piano on
// Cumulonimbus: input -> stereo grains (wet-only, WetHost dry/wet) -> one stereo Torus reverb on
// that mix (WetHost dry/wet) -> output guard. The effects stay replaceable components; this file
// owns only routing, mixing, the Freeze/Run/Bypass policy and diagnostic stage isolation.
//
// Bypass policy (SW18 down): the feeds of both effects fade out over 10 ms, so no new material
// enters, and the wet tails ring on for kTailBlocks (2 s, the ring length). Then both wet mixes
// glide to 0 over 10 ms and the wet work is suspended: the output is the exact input. Leaving
// Bypass during the tail just fades the feeds back in. Leaving it while suspended resumes the
// work with feeds and mixes gliding up from 0; the ring holds silence by then, so the grains
// warm up as it refills (position 0 reads the newest audio first).
// Freeze (SW18 up) holds the ring; grains and reverb keep running.
#include "../../../common/audio/wet_host.h"
#include "../../torus-reverb/src/torus_reverb.h"
#include "cumulonimbus.h"
#include <cmath>
#include <cstddef>

#define CUMULONIMBUS_CHAIN_VERSION "0.1.0"

namespace seed::fx {

enum class ChainMode : uint8_t { Run, Freeze, Bypass };

// Diagnostic stage isolation. A stage that is off does no work (its output is its input);
// mute_wet keeps all work running with both wet mixes at 0.
struct ChainRoute {
    bool grains = true, reverb = true;
    bool grain_write = true, grain_render = true;
    bool mute_wet = false;
};

struct ChainSettings {
    float grain_mix = .2f, hall_mix = .2f, hall_decay = .5f;
};

// Identity up to |x| = 0.8 (the Toy Piano ceiling), so a dry signal passes bit-exactly; above, a
// soft knee towards 1.0.
inline float OutputGuard(float x) {
    constexpr float kKnee = .8f, kRoom = .2f;
    const float a = std::fabs(x);
    if(!(a > kKnee))
        return a == a ? x : 0.f; // NaN -> 0
    const float y = kKnee + kRoom * std::tanh((a - kKnee) / kRoom);
    return x < 0 ? -y : y;
}

class GranularReverbChain {
  public:
    using Grains = Cumulonimbus;
    static constexpr std::size_t kMaxBlock = 48;
    static constexpr unsigned kTailBlocks = 2000, kFadeBlocks = 12; // at 1 kHz blocks
    enum class Phase : uint8_t { Active, Tail, Fade, Suspended };

    GranularReverbChain() : grain_host_(grains_), reverb_host_(reverb_) {}

    bool Prepare(Grains::Storage& grain_storage, TorusReverb::Storage& reverb_storage,
                 Grains::Options options = {}) {
        const audio::Config config{48000, kMaxBlock};
        if(!grains_.Prepare(config, grain_storage, options) || !reverb_.Prepare(config, reverb_storage))
            return false;
        grain_host_.Reset(true, settings_.grain_mix);
        reverb_host_.Reset(true, settings_.hall_mix);
        phase_ = Phase::Active;
        tail_ = 0;
        ApplyHall();
        return true;
    }

    void SetGrainParameters(const CumulonimbusParameters& p) {
        CumulonimbusParameters q = p;
        q.freeze = mode_ == ChainMode::Freeze;
        grains_.SetParameters(q);
    }
    void SetSettings(const ChainSettings& s) {
        settings_ = s;
        ApplyHall();
    }
    void SetMode(ChainMode mode) {
        if(mode == mode_)
            return;
        mode_ = mode;
        CumulonimbusParameters p = grains_.GetParameters();
        p.freeze = mode == ChainMode::Freeze;
        grains_.SetParameters(p);
    }
    void SetRoute(const ChainRoute& route) {
        route_ = route;
        grains_.SetWriteEnabled(route.grain_write);
        grains_.SetRenderEnabled(route.grain_render);
    }
    void Trigger() { grains_.Trigger(); }
    void SetCap(int32_t cap) { grains_.SetCap(cap); }
    void SetClock(Grains::Clock clock) {
        clock_ = clock;
        grains_.SetClock(clock);
    }

    // Exact per-channel in-place use is allowed. Longer blocks are not processed.
    void Process(const float* left, const float* right, float* out_left, float* out_right, std::size_t frames) {
        if(frames > kMaxBlock)
            return;
        reverb_cycles_ = 0;
        UpdatePhase();
        if(phase_ == Phase::Suspended) {
            for(std::size_t i = 0; i < frames; ++i) {
                out_left[i] = left[i];
                out_right[i] = right[i];
            }
            return;
        }
        const bool feeding = mode_ != ChainMode::Bypass;
        const bool muted = route_.mute_wet || phase_ == Phase::Fade;
        grain_host_.SetEnabled(feeding);
        reverb_host_.SetEnabled(feeding);
        grain_host_.SetMix(muted ? 0.f : settings_.grain_mix);
        reverb_host_.SetMix(muted ? 0.f : settings_.hall_mix);
        float l[kMaxBlock], r[kMaxBlock];
        if(route_.grains) {
            grain_host_.Process(left, right, l, r, frames);
        } else {
            for(std::size_t i = 0; i < frames; ++i) {
                l[i] = left[i];
                r[i] = right[i];
            }
        }
        if(route_.reverb) {
            const uint32_t t0 = clock_ ? clock_() : 0u;
            reverb_host_.Process(l, r, l, r, frames);
            reverb_cycles_ = clock_ ? clock_() - t0 : 0u;
        }
        for(std::size_t i = 0; i < frames; ++i) {
            out_left[i] = OutputGuard(l[i]);
            out_right[i] = OutputGuard(r[i]);
        }
    }

    Phase GetPhase() const { return phase_; }
    bool Suspended() const { return phase_ == Phase::Suspended; }
    ChainMode Mode() const { return mode_; }
    const ChainRoute& Route() const { return route_; }
    Grains& GrainEffect() { return grains_; }
    const Grains& GrainEffect() const { return grains_; }
    uint32_t ReverbCycles() const { return reverb_cycles_; }

  private:
    void UpdatePhase() {
        if(mode_ != ChainMode::Bypass) {
            phase_ = Phase::Active; // a suspended chain resumes with feeds/mixes gliding from 0
            tail_ = 0;
            return;
        }
        switch(phase_) {
            case Phase::Active: phase_ = Phase::Tail; tail_ = 0; break;
            case Phase::Tail:
                if(++tail_ >= kTailBlocks) { phase_ = Phase::Fade; tail_ = 0; }
                break;
            case Phase::Fade:
                if(++tail_ >= kFadeBlocks) phase_ = Phase::Suspended;
                break;
            case Phase::Suspended: break;
        }
    }
    void ApplyHall() {
        TorusReverbParameters p;
        p.decay = settings_.hall_decay;
        reverb_.SetParameters(p);
    }

    Grains grains_;
    TorusReverb reverb_;
    audio::WetHost<Grains, kMaxBlock> grain_host_;
    audio::WetHost<TorusReverb, kMaxBlock> reverb_host_;
    ChainSettings settings_{};
    ChainRoute route_{};
    ChainMode mode_ = ChainMode::Run;
    Phase phase_ = Phase::Active;
    unsigned tail_ = 0;
    Grains::Clock clock_ = nullptr;
    uint32_t reverb_cycles_ = 0;
};

} // namespace seed::fx
