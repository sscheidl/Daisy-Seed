#pragma once
// SPDX-License-Identifier: MIT
// Board-independent diagnostics of the Cumulonimbus images: scenario routes, the channel-16 CC
// control surface, the deterministic built-in test source and the paced telemetry sender.
// Shared by the standalone firmware and the Toy Piano on Cumulonimbus profiling images.
#include "chain.h"
#include "stage_meter.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cumulonimbus::diag {

// Scenario numbers (CC20 on channel 16, or the standalone keys S1..S7).
enum Scenario : uint8_t {
    kScenarioFull,       // grains + reverb
    kScenarioDry,        // both stages off: no wet work, exact input (Toy Piano: the dry route)
    kScenarioGrainsOnly, // grains, reverb stage off
    kScenarioReverbOnly, // reverb, grain stage off
    kScenarioWriteOnly,  // grain ring writes, no grain rendering, reverb off
    kScenarioRenderOnly, // grain rendering from a held ring, reverb off
    kScenarioMuted,      // full work, both wet mixes forced to 0 (exact dry output)
    kScenarioCount
};

inline seed::fx::ChainRoute RouteFor(uint8_t scenario) {
    seed::fx::ChainRoute r;
    switch(scenario) {
        case kScenarioDry: r.grains = r.reverb = false; break;
        case kScenarioGrainsOnly: r.reverb = false; break;
        case kScenarioReverbOnly: r.grains = false; break;
        case kScenarioWriteOnly: r.reverb = false; r.grain_render = false; break;
        case kScenarioRenderOnly: r.reverb = false; r.grain_write = false; break;
        case kScenarioMuted: r.mute_wet = true; break;
        default: break;
    }
    return r;
}

// Main loop -> audio thread requests (-1 = none), applied at the next block start. The statistics
// reset has its own flag; a snapshot request never travels through here: the main loop asks the
// meter directly (StageMeter::RequestSnapshot), so neither can swallow or overwrite the other
// (PR #108 review R2).
struct Requests {
    std::atomic<int32_t> scenario{-1}, cap{-1}, stimulus{-1};
    std::atomic<bool> reset{false};
};
static_assert(std::atomic<int32_t>::is_always_lock_free, "requests must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "requests must be lock-free");

struct ControlResult {
    bool consumed = false; // the CC belonged to the diagnostics
    bool snapshot = false; // main loop: call StageMeter::RequestSnapshot()
};

// Channel-16 control change (channel 15 zero-based), main loop.
inline ControlResult HandleControlChange(Requests& q, uint8_t channel, uint8_t cc, uint8_t value) {
    ControlResult r;
    if(channel != 15)
        return r;
    r.consumed = true;
    switch(cc) {
        case ControlCc::kScenario:
            if(value < kScenarioCount) q.scenario.store(value, std::memory_order_relaxed);
            break;
        case ControlCc::kCap:
            if(value <= 64) q.cap.store(value, std::memory_order_relaxed);
            break;
        case ControlCc::kStimulus: q.stimulus.store(value, std::memory_order_relaxed); break;
        case ControlCc::kTelemetry:
            if(value == 1) r.snapshot = true;
            else if(value == 2) q.reset.store(true, std::memory_order_release);
            break;
        default: r.consumed = false; break;
    }
    return r;
}

// Deterministic stereo test source (standalone images). Patterns: 0 external input (not
// generated), 1 impulses (L at 0 ms, R at 250 ms, every 500 ms), 2 tone bursts (L 440 Hz,
// R 660 Hz, 50 ms every 250 ms, alternating sides), 3 independent white noise L/R at -12 dBFS.
class TestSource {
  public:
    static constexpr uint8_t kExternal = 0, kImpulses = 1, kBursts = 2, kNoise = 3, kPatterns = 4;
    void Select(uint8_t pattern) {
        pattern_ = pattern < kPatterns ? pattern : kExternal;
        frame_ = 0;
        phase_l_ = phase_r_ = 0.f;
        noise_l_ = 0x1234567u;
        noise_r_ = 0x7654321u;
    }
    uint8_t Pattern() const { return pattern_; }
    bool External() const { return pattern_ == kExternal; }
    void Process(float* left, float* right, std::size_t frames) {
        for(std::size_t i = 0; i < frames; ++i, ++frame_) {
            float l = 0.f, r = 0.f;
            const uint32_t t = frame_ % 24000u; // 500 ms at 48 kHz
            switch(pattern_) {
                case kImpulses:
                    l = t == 0 ? .9f : 0.f;
                    r = t == 12000 ? .9f : 0.f;
                    break;
                case kBursts: {
                    const uint32_t q = frame_ % 12000u; // 250 ms
                    const bool on = q < 2400u;          // 50 ms
                    const bool left_turn = (frame_ / 12000u) % 2u == 0;
                    phase_l_ += 440.f / 48000.f;
                    phase_r_ += 660.f / 48000.f;
                    phase_l_ -= phase_l_ >= 1.f ? 1.f : 0.f;
                    phase_r_ -= phase_r_ >= 1.f ? 1.f : 0.f;
                    if(on && left_turn) l = .5f * Sine(phase_l_);
                    if(on && !left_turn) r = .5f * Sine(phase_r_);
                    break;
                }
                case kNoise:
                    l = .25f * Noise(noise_l_);
                    r = .25f * Noise(noise_r_);
                    break;
                default: break;
            }
            left[i] = l;
            right[i] = r;
        }
    }

  private:
    static float Sine(float phase) { // parabolic approximation, plenty for a test tone
        const float x = phase < .5f ? phase * 4.f - 1.f : 3.f - phase * 4.f;
        return x * (1.5f - .5f * x * x);
    }
    static float Noise(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return float(int32_t(s)) * (1.f / 2147483648.f);
    }
    uint8_t pattern_ = kExternal;
    uint32_t frame_ = 0, noise_l_ = 1, noise_r_ = 2;
    float phase_l_ = 0.f, phase_r_ = 0.f;
};

// Main loop: sends one telemetry record per Service call, so a snapshot of kRecords records
// never floods the USB transmit path.
class TelemetrySender {
  public:
    void Start(const Metrics& m) {
        metrics_ = m;
        next_ = 0;
        active_ = true;
    }
    bool Busy() const { return active_; }
    template <class Send> void Service(Send&& send) {
        if(!active_)
            return;
        auto packet = EncodeRecord(metrics_, next_);
        send(packet.data(), packet.size());
        if(++next_ >= kRecords)
            active_ = false;
    }

  private:
    Metrics metrics_{};
    unsigned next_ = 0;
    bool active_ = false;
};

} // namespace cumulonimbus::diag
