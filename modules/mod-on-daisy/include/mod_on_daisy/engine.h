#pragma once
// Audio side of Mod on Daisy (ARCHITECTURE.md § 4-6). Only the audio IRQ calls
// Tick/Apply*; the main loop only writes through Programs() and LiveState.
#include "mailbox.h"
#include "notes.h"
#include "program.h"

namespace mod {
struct MonitorSnapshot {
    float source[kSources]{};
    float offset[kTargets]{};
    float cv_raw[2]{}; // before calibration and dead band (CV calibration, TEST_PLAN H5)
#if MOD_ON_DAISY_EXTENSIONS
    // Extended monitor (PROTOCOL.md § 3.3).
    float base[kTargets]{-1, -1, -1, -1, -1, -1, -1, -1}; // 0..1 before modulation; -1 = not reported
    float min[kSources]{}, max[kSources]{};               // range of each source in the monitor window
    uint8_t clocks = 0; // bit 0 MIDI clock, bit 1 gate 1, bit 2 gate 2 running
#endif
};

class Engine {
  public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void Init(LiveState& live, float sample_rate, std::size_t block_size) {
        live_ = &live;
        sample_rate_ = sample_rate;
        UpdateCoefficients(block_size);
        last_note_ons_ = live.NoteOns();
        last_starts_ = live.Starts();
        last_clocks_ = live.Clocks();
    }
    // Main loop: write a compiled program into Programs().Back(), then Publish().
    TripleBuffer<Program>& Programs() { return programs_; }
    TripleBuffer<MonitorSnapshot>& Monitor() { return monitor_; }
    void EnableMonitor(bool on) { monitor_on_.store(on, std::memory_order_relaxed); }

    // Audio IRQ, once per block after hw.UpdateAllControls().
    void SetHardware(float cv1, float cv2, bool gate1, bool gate2) {
        cv_raw_[0] = cv1;
        cv_raw_[1] = cv2;
        gate_now_[0] = gate1;
        gate_now_[1] = gate2;
    }

    void Tick(std::size_t samples) {
        if(programs_.Fetch())
            program_ = &programs_.Front();
        const Program& p = *program_;
        if(samples != tick_samples_)
            UpdateCoefficients(samples); // only when the instrument changes its tick length
        FollowClocks(samples);
        const uint32_t note_ons = live_->NoteOns();
        const bool retrigger = note_ons != last_note_ons_;
        last_note_ons_ = note_ons;

        if(!p.active) {
            if(was_active_) {
                for(std::size_t t = 0; t < kTargets; ++t)
                    offset_[t] = 0.0f;
                for(float& s : source_)
                    s = 0.0f;
                was_active_ = false;
            }
            for(std::size_t i = 0; i < 2; ++i) // keep free LFOs running in phase
                lfo_[i].phase += p.lfo[i].inc * uint32_t(samples);
            random_.phase += p.random.inc * uint32_t(samples);
            PublishMonitor();
            return;
        }
        was_active_ = true;

#if MOD_ON_DAISY_EXTENSIONS
        // Cross modulation uses the other LFO's value of the previous tick.
        const float previous[2] = {source_[Lfo1], source_[Lfo2]};
#endif
        for(std::size_t i = 0; i < 2; ++i) {
            bool wrapped = false;
#if MOD_ON_DAISY_EXTENSIONS
            uint32_t inc = p.lfo[i].inc;
            if(p.lfo[i].fm_octaves < 0.0f || p.lfo[i].fm_octaves > 0.0f) { // exactly 0: rate untouched
                const float scaled = float(inc) * Exp2Small(p.lfo[i].fm_octaves * previous[1 - i]);
                inc = scaled >= 4294967295.0f ? 0xFFFFFFFFu : uint32_t(scaled);
            }
            const float phase = RunPhase(p.lfo[i], lfo_[i], samples, retrigger, wrapped, inc);
#else
            const float phase = RunPhase(p.lfo[i], lfo_[i], samples, retrigger, wrapped);
#endif
            if(p.lfo[i].shape == SampleHold) {
                if(wrapped || !lfo_[i].primed) {
                    lfo_[i].hold = lfo_[i].rng.Bipolar();
                    lfo_[i].primed = true;
                }
                source_[Lfo1 + i] = lfo_[i].hold;
            } else {
                source_[Lfo1 + i] = ShapeValue(p.lfo[i].shape, phase);
            }
        }
        {
            bool wrapped = false;
#if MOD_ON_DAISY_EXTENSIONS
            const float phase = RunPhase(p.random, random_, samples, false, wrapped, p.random.inc);
#else
            const float phase = RunPhase(p.random, random_, samples, false, wrapped);
#endif
            if(wrapped || !random_.primed) {
                random_.previous = random_.primed ? random_.hold : random_.rng.Bipolar();
                random_.hold = random_.rng.Bipolar();
                random_.primed = true;
            }
            source_[Random] = p.random.shape == Smooth
                ? random_.previous + (random_.hold - random_.previous) * phase
                : random_.hold;
        }
        for(std::size_t i = 0; i < 2; ++i)
            source_[Cv1 + i] = cv_[i].Process(CvDeadBand(cv_raw_[i] - p.cv_offset[i]), cv_coeff_);
        constexpr float kMidi = 1.0f / 127.0f;
        source_[ModWheel] = mod_wheel_.Process(float(live_->ModWheelRaw()) * kMidi, midi_coeff_);
        source_[ChannelAt] = channel_at_.Process(float(live_->ChannelPressureRaw()) * kMidi, midi_coeff_);
        source_[PolyAt] = last_pressure_.Process(float(live_->LastPressureRaw()) * kMidi, midi_coeff_);

        for(std::size_t t = 0; t < kTargets; ++t)
            offset_[t] = 0.0f;
        for(std::size_t c = 0; c < p.count; ++c)
            offset_[p.conn[c].target] += p.conn[c].gain * source_[p.conn[c].source];
        PublishMonitor();
    }

    // Target value in the instrument's normalized range. With no active
    // connection this returns `base` bit for bit (offset is +0.0f).
    float Apply(std::size_t target, float base) const { return Clamp01(base + offset_[target]); }
    float ApplyVoice(std::size_t target, float base, float voice_pressure) const {
        return Clamp01(base + offset_[target] + program_->voice_gain[target] * voice_pressure);
    }
    float VoiceGain(std::size_t target) const { return program_->voice_gain[target]; }
    // Audio IRQ, optional: the column's value before modulation, for the monitor only.
#if MOD_ON_DAISY_EXTENSIONS
    void ReportBase(std::size_t target, float base) { base_[target] = base; }
    // Main loop: starts a new min/max window once a monitor frame has been taken. The
    // audio side resets its own accumulators when it sees the new epoch (no shared
    // write), so a window covers the ticks between two frames.
    void NextMonitorWindow() { range_epoch_.fetch_add(1, std::memory_order_relaxed); }
#else
    void ReportBase(std::size_t, float) {}
#endif
    float NotePressure(uint8_t note) const { return float(live_->PressureRaw(note)) * (1.0f / 127.0f); }
    float PressureCoeff() const { return midi_coeff_; }
    bool Active() const { return program_->active; }
    float Source(std::size_t s) const { return source_[s]; }
    float Offset(std::size_t t) const { return offset_[t]; }
    float SampleRate() const { return sample_rate_; }
    std::size_t TickSamples() const { return tick_samples_; }
    const ClockFollower& MidiClockFollower() const { return midi_clock_; }
    const ClockFollower& GateFollower(std::size_t i) const { return gate_clock_[i]; }

  private:
    struct Voice {
        uint32_t phase = 0;
        float last = 0.0f, hold = 0.0f, previous = 0.0f;
        bool primed = false;
        Rng rng;
    };

#if MOD_ON_DAISY_EXTENSIONS
    float RunPhase(const SourceConfig& c, Voice& v, std::size_t samples, bool retrigger, bool& wrapped,
                   uint32_t inc) {
#else
    float RunPhase(const SourceConfig& c, Voice& v, std::size_t samples, bool retrigger, bool& wrapped) {
#endif
        float phase;
        if(c.sync == Free) {
            const uint32_t old = v.phase;
            if(c.retrigger && retrigger) {
                v.phase = 0;
                wrapped = true;
            } else {
#if MOD_ON_DAISY_EXTENSIONS
                v.phase += inc * uint32_t(samples);
#else
                v.phase += c.inc * uint32_t(samples);
#endif
                wrapped = v.phase < old;
            }
            phase = float(v.phase >> 8) * (1.0f / 16777216.0f);
        } else {
            const ClockFollower& clock = c.sync == MidiClock ? midi_clock_ : gate_clock_[c.gate & 1];
            phase = clock.Phase(c.ratio);
            wrapped = phase < v.last;
        }
        v.last = phase;
        return phase;
    }

    void FollowClocks(std::size_t samples) {
        midi_clock_.Advance(samples);
        const uint32_t starts = live_->Starts(); // acquire: ClockAtStart() is current
        const uint32_t clocks = live_->Clocks();
        if(starts != last_starts_) {
            last_starts_ = starts;
            midi_clock_.Restart();
            midi_clock_.Pulses(clocks - live_->ClockAtStart());
        } else {
            midi_clock_.Pulses(clocks - last_clocks_);
        }
        last_clocks_ = clocks;
        for(std::size_t i = 0; i < 2; ++i) {
            gate_clock_[i].Advance(samples);
            if(gate_now_[i] && !gate_before_[i])
                gate_clock_[i].Pulses(1);
            gate_before_[i] = gate_now_[i];
        }
    }

    void UpdateCoefficients(std::size_t samples) {
        tick_samples_ = samples;
        cv_coeff_ = SmoothingCoeff(0.002f, samples, sample_rate_);
        midi_coeff_ = SmoothingCoeff(0.010f, samples, sample_rate_);
    }

    void PublishMonitor() {
        if(!monitor_on_.load(std::memory_order_relaxed))
            return;
        MonitorSnapshot& m = monitor_.Back();
        for(std::size_t i = 0; i < kSources; ++i)
            m.source[i] = source_[i];
        for(std::size_t i = 0; i < kTargets; ++i)
            m.offset[i] = offset_[i];
        m.cv_raw[0] = cv_raw_[0];
        m.cv_raw[1] = cv_raw_[1];
#if MOD_ON_DAISY_EXTENSIONS
        for(std::size_t i = 0; i < kTargets; ++i)
            m.base[i] = base_[i];
        const uint32_t epoch = range_epoch_.load(std::memory_order_relaxed);
        for(std::size_t i = 0; i < kSources; ++i) {
            if(epoch != range_seen_ || source_[i] < range_min_[i])
                range_min_[i] = source_[i];
            if(epoch != range_seen_ || source_[i] > range_max_[i])
                range_max_[i] = source_[i];
            m.min[i] = range_min_[i];
            m.max[i] = range_max_[i];
        }
        range_seen_ = epoch;
        m.clocks = uint8_t((Running(midi_clock_, 3.0f, 0.25f) ? 1 : 0) | (Running(gate_clock_[0], 2.5f, 0.05f) ? 2 : 0)
                           | (Running(gate_clock_[1], 2.5f, 0.05f) ? 4 : 0));
#endif
        monitor_.Publish();
    }

#if MOD_ON_DAISY_EXTENSIONS
    // A clock runs while its next pulse is not overdue: `periods` expected periods,
    // at least `min_seconds`, at most 4 s (slow gate clocks).
    bool Running(const ClockFollower& c, float periods, float min_seconds) const {
        if(!c.HasReference())
            return false;
        float limit = c.PeriodSamples() * periods;
        if(limit < min_seconds * sample_rate_)
            limit = min_seconds * sample_rate_;
        if(limit > 4.0f * sample_rate_)
            limit = 4.0f * sample_rate_;
        return c.SinceSamples() < limit;
    }
#endif

    LiveState* live_ = nullptr;
    TripleBuffer<Program> programs_;
    const Program* program_ = &programs_.Front();
    TripleBuffer<MonitorSnapshot> monitor_;
    std::atomic<bool> monitor_on_{false};
    float sample_rate_ = 48000.0f;
    std::size_t tick_samples_ = 0;
    float cv_coeff_ = 1.0f, midi_coeff_ = 1.0f;
    float cv_raw_[2]{};
    bool gate_now_[2]{}, gate_before_[2]{};
    float source_[kSources]{};
    float offset_[kTargets]{};
#if MOD_ON_DAISY_EXTENSIONS
    float base_[kTargets]{-1, -1, -1, -1, -1, -1, -1, -1};
    std::atomic<uint32_t> range_epoch_{0}; // written by the main loop only
    uint32_t range_seen_ = ~0u;            // audio only, like the accumulators
    float range_min_[kSources]{}, range_max_[kSources]{};
#endif
    bool was_active_ = false;
    Voice lfo_[2]{Voice{0, 0, 0, 0, false, Rng(0x9E3779B9u)}, Voice{0, 0, 0, 0, false, Rng(0x7F4A7C15u)}};
    Voice random_{0, 0, 0, 0, false, Rng(0x2545F491u)};
    Smoother cv_[2], mod_wheel_, channel_at_, last_pressure_;
    ClockFollower midi_clock_{0.125f};
    ClockFollower gate_clock_[2]{ClockFollower(0.5f), ClockFollower(0.5f)};
    uint32_t last_note_ons_ = 0, last_starts_ = 0, last_clocks_ = 0;
};

// Per-voice Poly AT smoothing for instruments (ARCHITECTURE.md § 5.2).
class VoicePressure {
  public:
    float Process(const Engine& engine, int note) {
        const float target = note >= 0 ? engine.NotePressure(uint8_t(note)) : 0.0f;
        return smoother_.Process(target, engine.PressureCoeff());
    }
    void Reset() { smoother_.Reset(); }
    float Value() const { return smoother_.Value(); }
  private:
    Smoother smoother_;
};
}
