#pragma once
// SPDX-License-Identifier: MIT
// Cumulonimbus: stereo wet-only granular effect under FX contract 1.0. The DSP is the Clouds
// granular path (src/clouds/, MIT, provenance in ../UPSTREAM.md); this adapter owns the PCM16
// rings, the Clouds granular-mode parameter mapping, the call split and the optional 32 kHz
// internal path. Dry/wet, gains, transitions, output protection and controls are the host's.
//
// Build-time internal rate (one codebase, two configurations):
//   CUMULONIMBUS_INTERNAL_RATE=48000  native: the grain kernel runs at the 48 kHz host rate in
//                                     calls of kSubBlock = 24 frames (Clouds' ceiling is 32).
//   CUMULONIMBUS_INTERNAL_RATE=32000  the host stays at 48 kHz; a fixed band-limited 48 -> 32 kHz
//                                     converter feeds the kernel in 32-frame calls (one per
//                                     48-frame host block) and 32 -> 48 kHz returns the wet signal.
// Durations, pitch, ring length and smoother times are in rate-correct units in both builds.
#include "../../../common/audio/fx_contract.h"
#include "clouds/live_scheduler.h"
#include "rate_converter.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#ifndef CUMULONIMBUS_INTERNAL_RATE
#define CUMULONIMBUS_INTERNAL_RATE 48000
#endif

namespace seed::fx {

struct CumulonimbusParameters {
    float position = .25f;      // Clouds POSITION, 0 = newest audio
    float size = .5f;           // Clouds SIZE, 32 ms .. 512 ms
    float density = .65f;       // Clouds DENSITY: < .47 regular, .47-.53 none, > .53 random
    float pitch = 0.f;          // semitones, -24 .. +24
    float texture = .5f;        // Clouds TEXTURE (window shape); the diffuser above .75 is not built
    float stereo_spread = .7f;  // random grain pan width
    bool freeze = false;        // hold the ring; grains keep playing
};

struct CumulonimbusTelemetry {
    int32_t active = 0, peak_active = 0, cap = 0;
    uint32_t started_high = 0, started_medium = 0, rejected = 0;
    uint32_t write_cycles = 0, render_cycles = 0, convert_cycles = 0; // last block, when clocked
};

template <unsigned InternalRate> class CumulonimbusT {
  public:
    static_assert(InternalRate == 48000 || InternalRate == 32000, "internal rate is 48000 or 32000");
    using Parameters = CumulonimbusParameters;
    static constexpr unsigned kHostRate = 48000;
    static constexpr unsigned kInternalRate = InternalRate;
    static constexpr std::size_t kHostMaxBlock = 48;
    static constexpr std::size_t kSubBlock = InternalRate == 48000 ? 24 : 32;
    static constexpr float kRingSeconds = 2.f;
    static constexpr int32_t kRingFrames = int32_t(InternalRate * 2) + cumulonimbus::clouds::kInterpolationTail;
    static constexpr int32_t kReferenceStereo = 32, kReferenceMono = 40; // Clouds' hi-fi pools
    static_assert(kSubBlock <= cumulonimbus::clouds::kMaxBlockSize, "Clouds renders at most 32 frames per call");

    struct Storage { // bulk PCM: the host places it (SDRAM on the kit)
        int16_t ring[2][kRingFrames];
        int16_t tail[2][cumulonimbus::clouds::kCrossFadeSize];
    };
    struct Options {
        unsigned channels = 2; // 1: mono ring from the left input, mono-to-stereo pan law
        int32_t cap = 32;      // grain pool bound, 0..64
        uint32_t seed = 0x5EED0001u;
    };
    using Clock = uint32_t (*)();

    bool Prepare(audio::Config config, Storage& storage, Options options = {}) {
        ready_ = false;
        if(config.sample_rate != kHostRate || !config.max_block || config.max_block > kHostMaxBlock ||
           (options.channels != 1 && options.channels != 2))
            return false;
        if(InternalRate == 32000 && config.max_block % 3)
            return false;
        config_ = config;
        channels_ = options.channels;
        tables_.Init(float(InternalRate));
        for(unsigned c = 0; c < 2; ++c)
            ring_[c].Init(storage.ring[c], kRingFrames, storage.tail[c]);
        scheduler_.Init(&tables_, int32_t(channels_), channels_ == 1 ? kReferenceMono : kReferenceStereo,
                        float(InternalRate), kSubBlock, options.seed);
        scheduler_.SetCap(options.cap);
        prototype_.Init();
        for(auto& d : down_) d.Init(prototype_);
        for(auto& u : up_) u.Init(prototype_);
        trigger_ = false;
        write_enabled_ = render_enabled_ = true;
        ready_ = true;
        SetParameters(parameters_);
        return true;
    }

    void SetParameters(Parameters p) {
        p.position = Unit(p.position, .25f);
        p.size = Unit(p.size, .5f);
        p.density = Unit(p.density, .65f);
        p.texture = Unit(p.texture, .5f);
        p.stereo_spread = Unit(p.stereo_spread, .7f);
        p.pitch = std::isfinite(p.pitch) ? std::max(-24.f, std::min(24.f, p.pitch)) : 0.f;
        parameters_ = p;
        // Clouds GranularProcessor::ProcessGranular, PLAYBACK_MODE_GRANULAR.
        grain_.position = p.position;
        grain_.size = p.size;
        grain_.pitch = p.pitch;
        grain_.stereo_spread = p.stereo_spread;
        grain_.use_deterministic_seed = p.density < .5f;
        if(p.density >= .53f)
            grain_.overlap = (p.density - .53f) * 2.12f;
        else if(p.density <= .47f)
            grain_.overlap = (.47f - p.density) * 2.12f;
        else
            grain_.overlap = 0.f;
        grain_.window_shape = p.texture < .75f ? p.texture * 1.333f : 1.f;
    }
    const Parameters& GetParameters() const { return parameters_; }

    void Trigger() { trigger_ = true; } // one grain at the next block start, applied once
    void SetCap(int32_t cap) { scheduler_.SetCap(cap); }
    int32_t Cap() const { return scheduler_.Cap(); }
    unsigned Channels() const { return channels_; }
    // Diagnostic stage split: ring writes held (render-only) or grains not rendered (write-only).
    void SetWriteEnabled(bool on) { write_enabled_ = on; }
    void SetRenderEnabled(bool on) { render_enabled_ = on; }
    void SetClock(Clock clock) { clock_ = clock; }

    bool Process(audio::StereoBlock block) {
        if(!ready_ || !audio::Valid(config_, block) || (InternalRate == 32000 && block.frames % 3)) {
            if(audio::Valid(config_, block))
                for(std::size_t i = 0; i < block.frames; ++i)
                    block.out_left[i] = block.out_right[i] = 0.f;
            return false;
        }
        telemetry_.write_cycles = telemetry_.render_cycles = telemetry_.convert_cycles = 0;
        const bool write = write_enabled_ && !parameters_.freeze;
        if(InternalRate == 48000) {
            for(std::size_t offset = 0; offset < block.frames; offset += kSubBlock) {
                const std::size_t n = std::min(kSubBlock, block.frames - offset);
                Kernel(block.left + offset, block.right + offset, block.out_left + offset,
                       block.out_right + offset, n, write);
            }
        } else {
            float in_l[kHostMaxBlock], in_r[kHostMaxBlock], wet_l[kHostMaxBlock], wet_r[kHostMaxBlock];
            uint32_t t0 = Now();
            const std::size_t n = down_[0].Process(block.left, block.frames, in_l);
            if(channels_ == 2)
                down_[1].Process(block.right, block.frames, in_r);
            telemetry_.convert_cycles += Now() - t0;
            Kernel(in_l, channels_ == 2 ? in_r : in_l, wet_l, wet_r, n, write);
            t0 = Now();
            up_[0].Process(wet_l, n, block.out_left);
            up_[1].Process(wet_r, n, block.out_right);
            telemetry_.convert_cycles += Now() - t0;
        }
        trigger_ = false;
        Publish();
        return true;
    }

    CumulonimbusTelemetry Telemetry() const { return telemetry_; }
    void ResetCounters() {
        scheduler_.ResetCounters();
        Publish();
    }
    int32_t ActiveGrains() const { return scheduler_.ActiveGrains(); }
    const cumulonimbus::clouds::LiveScheduler& Scheduler() const { return scheduler_; }
    const cumulonimbus::clouds::PcmRing& Ring(unsigned c) const { return ring_[c]; }

  private:
    // One Clouds call of n <= kSubBlock frames at the internal rate.
    void Kernel(const float* left, const float* right, float* out_left, float* out_right, std::size_t n,
                bool write) {
        uint32_t t0 = Now();
        ring_[0].WriteFade(left, int32_t(n), 1, write);
        if(channels_ == 2)
            ring_[1].WriteFade(right, int32_t(n), 1, write);
        telemetry_.write_cycles += Now() - t0;
        t0 = Now();
        if(render_enabled_) {
            float interleaved[2 * kSubBlock];
            scheduler_.Play(ring_, grain_, trigger_, interleaved, n);
            trigger_ = false; // the one-shot belongs to the first call of the host block
            for(std::size_t i = 0; i < n; ++i) {
                out_left[i] = interleaved[2 * i];
                out_right[i] = interleaved[2 * i + 1];
            }
        } else {
            for(std::size_t i = 0; i < n; ++i)
                out_left[i] = out_right[i] = 0.f;
        }
        telemetry_.render_cycles += Now() - t0;
    }
    uint32_t Now() const { return clock_ ? clock_() : 0u; }
    void Publish() {
        const auto& c = scheduler_.Counters();
        telemetry_.active = c.active;
        telemetry_.peak_active = c.peak_active;
        telemetry_.cap = scheduler_.Cap();
        telemetry_.started_high = c.started_high;
        telemetry_.started_medium = c.started_medium;
        telemetry_.rejected = c.rejected;
    }
    static float Unit(float x, float fallback) {
        return std::isfinite(x) ? std::max(0.f, std::min(1.f, x)) : fallback;
    }

    audio::Config config_{};
    Parameters parameters_{};
    cumulonimbus::clouds::GrainParameters grain_{};
    cumulonimbus::clouds::Tables tables_;
    cumulonimbus::clouds::PcmRing ring_[2];
    cumulonimbus::clouds::LiveScheduler scheduler_;
    cumulonimbus::RatePrototype prototype_;
    cumulonimbus::Down48To32 down_[2];
    cumulonimbus::Up32To48 up_[2];
    CumulonimbusTelemetry telemetry_{};
    Clock clock_ = nullptr;
    unsigned channels_ = 2;
    bool trigger_ = false, write_enabled_ = true, render_enabled_ = true, ready_ = false;
};

using Cumulonimbus = CumulonimbusT<CUMULONIMBUS_INTERNAL_RATE>;

} // namespace seed::fx
