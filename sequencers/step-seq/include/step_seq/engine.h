#pragma once
// SPDX-License-Identifier: MIT
// Step Seq core: time base, transport state machine, clock sources and note events
// (ARBEITSAUFTRAG 3.3, 4.2-4.4; TRANSPORT.md). One Tick() per 48-frame audio block
// (1 ms). Header-only, no hardware, no allocation; runs in the audio callback.
#include "pattern.h"
#include <cmath>
#include <cstdint>

namespace step_seq {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlock = 48;            // samples per tick
constexpr uint32_t kMinGateHighTicks = 2;  // 2 ms
constexpr uint32_t kPreviewTicks = 150;
constexpr uint32_t kMinTimeoutTicks = 2000;
constexpr uint32_t kNever = 0xFFFFFFFFu;

enum class State : uint8_t { Stop, Waiting, Running, Paused };
enum class ClockSource : uint8_t { Internal, GateIn1, Midi, Auto };
enum class Port : uint8_t { Usb, Din };
// The source whose pulses currently move the sequencer (GET_STATE codes 0..3).
enum class Active : uint8_t { Internal, Gate, Usb, Din, None };
enum class Divider : uint8_t { Step, Ppq2, Ppq4, Ppq24 };
enum class Gate2Mode : uint8_t { Reset, Run };
enum class Cv1Mode : uint8_t { Transpose, Off };
enum class Cv2Mode : uint8_t { Off, GateLength, Swing };
enum class Source : uint8_t { Sequencer, Preview, MidiIn };

struct Settings {
    float tempo = 120.f;        // BPM 30..300 (VR5)
    float swing = 50.f;         // % 50..75 (VR6)
    int length = 16;            // 1..16 (VR7)
    int transpose = 0;          // -12..12 (VR8)
    Mode mode = Mode::Forward;  // SW18
    Scale scale = Scale::Chromatic;
    ClockSource clock_source = ClockSource::Internal; // D10
    Divider divider = Divider::Step;
    Gate2Mode gate2_mode = Gate2Mode::Reset;
    Cv1Mode cv1_mode = Cv1Mode::Transpose;
    Cv2Mode cv2_mode = Cv2Mode::Off;
    uint8_t seq_channel = 1, voice_channel = 2;
    bool usb_out = true, din_out = true;
    uint8_t accent_velocity = 127, velocity = 100;
    float glide_ms = 60.f;
    bool clock_out = true;
    int cv_low_note = 0, cv_high_note = 127; // calibrated CV range in MIDI notes
};

struct OutEvent {
    enum class Kind : uint8_t { NoteOn, NoteOff, Legato, Clock, Start, Stop, Continue, Panic };
    Kind kind = Kind::NoteOn;
    uint8_t note = 0;       // NoteOn/NoteOff/Legato: the (new) note
    uint8_t prev_note = 0;  // Legato: the note it replaces
    uint8_t velocity = 0;
    bool accent = false;
    Source source = Source::Sequencer;
    uint32_t tag = 0;       // NoteOn/Legato: new tag; NoteOff: tag of the ended note
    uint32_t prev_tag = 0;  // Legato: tag of the replaced note
};

struct TickInput {
    bool run_switch = false;  // SW17 on Run (debounced)
    bool gate1_rise = false;  // rising edge at Gate IN 1 in this block
    bool gate2_high = false;
    bool gate2_rise = false;
    int cv1_semitones = 0;    // quantized CV IN 1 transpose, -24..24
    float cv2 = 0.f;          // CV IN 2, -1..1 (+-5 V)
};

struct TickOutput {
    static constexpr int kMaxEvents = 16;
    bool gate = false;          // CV OUT 2 high
    uint8_t pitch = 48;         // CV OUT 1 target (MIDI note)
    bool glide = false;         // slide toward pitch in this tick
    bool step_changed = false;
    int8_t step = -1;           // 0..15, -1 none
    State state = State::Stop;
    Active active = Active::None;
    OutEvent events[kMaxEvents];
    int count = 0;
    uint32_t dropped = 0;
    void Push(const OutEvent& e) {
        if(count < kMaxEvents)
            events[count++] = e;
        else
            ++dropped;
    }
};

struct Counters {
    uint32_t clock_ignored = 0;   // clock from a port that is not the selected source
    uint32_t events_dropped = 0;  // TickOutput overflow
    uint32_t timeouts = 0;
};

class Engine {
  public:
    Settings settings;
    Pattern pattern{};
    Counters counters;

    void Init(uint32_t seed = Xorshift32::kDefaultSeed) {
        rng_.Seed(seed);
        playhead_.Clear();
        state_ = State::Stop;
        enabled_ = false;
        now_ = 0;
        ResetPeriodEstimate();
    }

    // ----- events from the adapter (before Tick); outputs follow in the next Tick() -----
    void MidiClock(Port port) {
        const ClockSource cs = settings.clock_source;
        if(cs != ClockSource::Midi && cs != ClockSource::Auto)
            return; // not a clock source: ignored without counting
        if(!AcceptMidiClock(port)) {
            ++counters.clock_ignored;
            return;
        }
        Pulse(port == Port::Usb ? Active::Usb : Active::Din);
    }
    void MidiStart(Port port) {
        if(!MidiTransportPort(port) || !enabled_)
            return;
        midi_port_ = PortActive(port);
        playhead_.Restart();
        step_count_ = 0;
        ResetPeriodEstimate();
        EnterRunningExternal(midi_port_);
        await_clock_ = true; // the start point plays with the first following clock
    }
    void MidiContinue(Port port) {
        if(!MidiTransportPort(port) || !enabled_)
            return;
        if(state_ != State::Paused && state_ != State::Waiting)
            return;
        midi_port_ = PortActive(port);
        EnterRunningExternal(midi_port_);
        await_clock_ = true;
    }
    void MidiStop(Port port) {
        if(!MidiTransportPort(port) || state_ != State::Running)
            return;
        Pause();
    }
    // Panel transport, effective only with enable. false: not allowed now (NAK 9).
    bool PanelStop() {
        if(!enabled_ || state_ != State::Running)
            return false;
        Pause();
        return true;
    }
    bool PanelStart() {
        if(!enabled_ || state_ != State::Paused)
            return false;
        playhead_.Restart();
        step_count_ = 0;
        if(UsesInternalStart())
            StartInternal();
        else
            EnterWaiting();
        return true;
    }
    // Stopped sequencer: sound a step for 150 ms (4.1). One slot only.
    void PreviewStep(int index) {
        if(state_ == State::Running || index < 0 || index >= kSteps)
            return;
        preview_request_ = index;
    }
    // Pattern from the panel: applied at the next step boundary (now while not running).
    void QueuePattern(const Pattern& p, uint16_t revision) {
        queued_pattern_ = p;
        queued_revision_ = revision;
        pattern_queued_ = true;
        if(state_ != State::Running)
            ApplyQueuedPattern();
    }
    // One step from the panel; stacks onto a pattern that is still waiting for its boundary.
    void QueueStep(int index, const Step& step, uint16_t revision) {
        if(index < 0 || index >= kSteps)
            return;
        Pattern next = pattern_queued_ ? queued_pattern_ : pattern;
        next[std::size_t(index)] = step;
        QueuePattern(next, revision);
    }
    // Parameters arrive at the block boundary; their revision counts as applied then.
    void ParameterApplied(uint16_t revision) {
        if(!pattern_queued_)
            applied_revision_ = revision;
        else
            queued_revision_ = revision; // reported with the pending pattern
    }
    uint16_t AppliedRevision() const { return applied_revision_; }
    void SetAppliedRevision(uint16_t r) { applied_revision_ = r; }
    // Bootloader entry, queue overflow, output changes: gates and notes off.
    void Panic() { panic_request_ = true; }

    State GetState() const { return state_; }
    Active GetActive() const { return state_ == State::Stop ? Active::None : active_; }
    int Position() const { return playhead_.Position(); }
    uint32_t Now() const { return now_; }
    bool GateHigh() const { return gate_high_; }
    bool Enabled() const { return enabled_; }
    // Current step period estimate in samples (internal tempo or external clock).
    double StepPeriodSamples() const { return ExternalActive() ? period_est_ : InternalStepSamples(); }
    float CurrentBpm() const { return float(kSampleRate * 60.0 / (StepPeriodSamples() * 4.0)); }
    // Start value of the external period estimate: the tempo of VR5 (4.3).
    void ResetPeriodEstimate() {
        period_est_ = InternalStepSamples();
        history_count_ = 0;
        history_pos_ = 0;
    }

    void Tick(const TickInput& in, TickOutput& out) {
        out.count = 0;
        out.step_changed = false;
        out.glide = false;
        ++now_;
        in_ = &in;
        out_ = &out;
        gate_fell_ = false;
        FlushPendingRt();

        // Transport: enable (SW17, Gate IN 2 in Run mode), clock pulses, timeouts.
        const bool enable = in.run_switch && (settings.gate2_mode != Gate2Mode::Run || in.gate2_high);
        // A clock source change while enabled restarts the transport like SW17 off and on:
        // notes end, the start point plays with the new source (internal) or it arms.
        if(enabled_ && settings.clock_source != enabled_source_)
            Disable();
        if(enable && !enabled_) {
            enabled_ = true;
            enabled_source_ = settings.clock_source;
            playhead_.Restart();
            step_count_ = 0;
            if(UsesInternalStart())
                StartInternal();
            else
                EnterWaiting();
        } else if(!enable && enabled_) {
            Disable();
        }
        if(in.gate1_rise)
            Pulse(Active::Gate);
        CheckTimeout();
        bool boundary = false;
        if(state_ == State::Running) {
            if(start_now_)
                boundary = true;
            if(active_ == Active::Internal) {
                pos_ += uint64_t(kBlock) << 32;
                if(pos_ > next_)
                    boundary = true;
            } else {
                if(external_boundary_)
                    boundary = true;
                if(scheduled_ != kNever && now_ >= scheduled_) {
                    boundary = true;
                    scheduled_ = kNever;
                }
            }
        }
        start_now_ = false;
        external_boundary_ = false;
        if(boundary && pending_source_ != Active::None) {
            SwitchToExternal(pending_source_); // Auto: an external source takes over
            pending_source_ = Active::None;
        }

        // 1. Gate-off of the old step, end of a preview, panic.
        if(end_preview_) {
            GateOff();
            end_preview_ = false;
        }
        if(panic_request_) {
            DoPanic();
            panic_request_ = false;
        }
        if(gate_high_ && now_ >= gate_off_at_)
            GateOff();
        if(pending_on_ && state_ != State::Running && next_source_ != Source::Preview)
            pending_on_ = false; // a step's delayed gate-on is void after stop or pause
        // 2. Reset (Gate IN 2 in Reset mode): the next step is the start point.
        if(settings.gate2_mode == Gate2Mode::Reset && in.gate2_rise) {
            playhead_.Restart();
            step_count_ = 0;
        }
        // 3. Delayed gate-on from the last tick, step change, 4. gate-on.
        if(pending_on_ && !gate_fell_) {
            pending_on_ = false;
            GateOn();
        }
        if(boundary && state_ == State::Running)
            DoStep();
        if(preview_request_ >= 0) {
            if(state_ != State::Running)
                StartPreview(preview_request_);
            preview_request_ = -1;
        }
        EmitClocks();

        out.gate = gate_high_;
        out.pitch = pitch_;
        out.state = state_;
        out.active = GetActive();
        out.step = int8_t(playhead_.Position() - 1);
        if(out.dropped) {
            counters.events_dropped += out.dropped;
            out.dropped = 0;
        }
        in_ = nullptr;
        out_ = nullptr;
    }

  private:
    // ----- transport -----
    static Active PortActive(Port port) { return port == Port::Usb ? Active::Usb : Active::Din; }
    bool UsesInternalStart() const {
        return settings.clock_source == ClockSource::Internal || settings.clock_source == ClockSource::Auto;
    }
    bool ExternalActive() const { return active_ == Active::Gate || active_ == Active::Usb || active_ == Active::Din; }

    void Disable() {
        enabled_ = false;
        if(state_ == State::Running && ClockOutActive())
            EmitRt(OutEvent::Kind::Stop);
        state_ = State::Stop;
        active_ = Active::None;
        midi_port_ = Active::None;
        pending_source_ = Active::None;
        panic_request_ = true;
        start_now_ = false;
        playhead_.Restart();
    }
    void StartInternal() {
        EndPreview();
        state_ = State::Running;
        active_ = Active::Internal;
        pos_ = 0;
        next_ = 0;
        timed_out_ = false;
        fallback_period_ = 0;
        start_now_ = true; // the start point plays in this (or the next) tick
        if(settings.clock_out)
            EmitRt(OutEvent::Kind::Start);
    }
    void EnterWaiting() {
        state_ = State::Waiting;
        active_ = Active::None;
        midi_port_ = Active::None;
        timed_out_ = false;
        await_clock_ = false;
        scheduled_ = kNever;
        ResetPeriodEstimate();
    }
    void EnterRunningExternal(Active source) {
        EndPreview();
        state_ = State::Running;
        active_ = source;
        scheduled_ = kNever;
        pulse_count_ = 0;
        last_pulse_ = now_;
        prev_pulse_ = kNever;
    }
    void Pause() {
        if(state_ == State::Running && ClockOutActive())
            EmitRt(OutEvent::Kind::Stop);
        state_ = State::Paused;
        panic_request_ = true;
        scheduled_ = kNever;
        await_clock_ = false;
        start_now_ = false;
    }
    bool MidiTransportPort(Port port) const {
        if(settings.clock_source != ClockSource::Midi)
            return false;
        return midi_port_ == Active::None || midi_port_ == PortActive(port);
    }
    // Ports that receive our own clock are no source in Auto mode (echo guard, 4.3).
    bool SendsClockTo(Active port) const {
        if(!settings.clock_out || state_ != State::Running ||
           !(active_ == Active::Internal || active_ == Active::Gate))
            return false;
        return (port == Active::Usb && settings.usb_out) || (port == Active::Din && settings.din_out);
    }
    bool AcceptMidiClock(Port port) const {
        const Active a = PortActive(port);
        if(settings.clock_source == ClockSource::Midi)
            return midi_port_ == Active::None || midi_port_ == a;
        return !SendsClockTo(a); // Auto
    }
    int PulsesPerStep() const {
        if(active_ == Active::Usb || active_ == Active::Din)
            return 6; // 24 PPQN
        switch(settings.divider) {
            case Divider::Ppq24: return 6;
            case Divider::Ppq2: return 0; // one pulse per two steps
            default: return 1;            // one pulse per step, or 4 per quarter
        }
    }
    static int Priority(Active a) { return a == Active::Gate ? 0 : (a == Active::Usb ? 1 : 2); }

    void Pulse(Active source) {
        if(!enabled_)
            return;
        const ClockSource cs = settings.clock_source;
        if(cs == ClockSource::Internal || (cs == ClockSource::GateIn1 && source != Active::Gate) ||
           (cs == ClockSource::Midi && source == Active::Gate))
            return;
        if(cs == ClockSource::Midi && midi_port_ == Active::None)
            midi_port_ = source; // the first port that delivers clock after arming
        if(cs == ClockSource::Auto) {
            if(state_ != State::Running)
                return;
            if(active_ == Active::Internal) {
                // A detected external source takes over at the next step boundary.
                if(pending_source_ == Active::None || Priority(source) < Priority(pending_source_))
                    pending_source_ = source;
                return;
            }
            if(source != active_) {
                ++counters.clock_ignored;
                return;
            }
        }
        if(state_ == State::Waiting) {
            if(cs == ClockSource::Midi && !timed_out_)
                return; // MIDI starts with Start or Continue; plain clocks arm the port only
            const bool resume = timed_out_;
            EnterRunningExternal(source);
            prev_pulse_ = now_;
            if(source == Active::Gate)
                EmitRt(resume ? OutEvent::Kind::Continue : OutEvent::Kind::Start);
            NominalBoundary();
            return;
        }
        if(state_ != State::Running || active_ != source)
            return;
        // Period estimate in samples per step: mean of the last four (4.3).
        if(!await_clock_ && prev_pulse_ != kNever && now_ > prev_pulse_) {
            const double interval = double(now_ - prev_pulse_) * kBlock;
            const int pps = PulsesPerStep();
            const double step = pps == 0 ? interval / 2.0 : interval * pps;
            history_[history_pos_++ & 3] = step;
            if(history_count_ < 4)
                ++history_count_;
            double sum = 0;
            for(int i = 0; i < history_count_; ++i)
                sum += history_[i];
            period_est_ = sum / history_count_;
            pulse_interval_ = now_ - prev_pulse_;
        }
        prev_pulse_ = now_;
        last_pulse_ = now_;
        if(await_clock_) {
            await_clock_ = false;
            pulse_count_ = 0;
            NominalBoundary();
            return;
        }
        ++pulse_count_;
        const int pps = PulsesPerStep();
        if(pps == 0 || pulse_count_ % pps == 0)
            NominalBoundary();
    }
    // A pulse-derived step boundary. The second step of a swing pair is delayed using the
    // period estimate; the first two steps after a start are straight (4.3).
    void NominalBoundary() {
        const int pps = PulsesPerStep();
        const double s = Swing() / 100.0;
        if(pps == 0) {
            external_boundary_ = true;
            // One pulse per two steps: the in-between step from the period estimate.
            scheduled_ = now_ + TicksOf(2.0 * period_est_ * s);
            return;
        }
        if(scheduled_ != kNever) {
            scheduled_ = kNever; // an early pulse: the delayed step plays now
            external_boundary_ = true;
            return;
        }
        if((step_count_ & 1) && step_count_ >= 3 && s > 0.5) {
            scheduled_ = now_ + TicksOf((2.0 * s - 1.0) * period_est_);
            return;
        }
        external_boundary_ = true;
    }
    void SwitchToExternal(Active source) {
        active_ = source;
        pulse_count_ = 0;
        scheduled_ = kNever;
        last_pulse_ = now_;
        prev_pulse_ = kNever;
        ResetPeriodEstimate();
    }
    void CheckTimeout() {
        if(state_ != State::Running || !ExternalActive() || await_clock_)
            return;
        uint32_t limit = 4 * pulse_interval_;
        if(limit < kMinTimeoutTicks)
            limit = kMinTimeoutTicks;
        if(now_ - last_pulse_ <= limit)
            return;
        ++counters.timeouts;
        scheduled_ = kNever;
        if(settings.clock_source == ClockSource::Auto) {
            // Phase-correct internal take-over: measured tempo, next step when expected.
            const double period = period_est_;
            active_ = Active::Internal;
            fallback_period_ = period;
            fallback_tempo_ = settings.tempo;
            // Phase-correct: the next step on the grid of the last measured period, counted
            // from the last boundary; skipped grid points keep the swing pairs in phase.
            const double since = double(now_ - last_boundary_) * kBlock;
            const double periods = std::ceil(since / period);
            if(periods > 1)
                step_count_ += uint32_t(periods) - 1;
            pos_ = uint64_t(since * 4294967296.0);
            next_ = uint64_t((periods < 1 ? 1 : periods) * period * 4294967296.0);
            return;
        }
        if(ClockOutActive())
            EmitRt(OutEvent::Kind::Stop);
        state_ = State::Waiting;
        timed_out_ = true;
        panic_request_ = true;
        if(settings.clock_source == ClockSource::Midi)
            midi_port_ = Active::None;
    }

    // ----- time base -----
    float Swing() const {
        float s = settings.swing;
        if(settings.cv2_mode == Cv2Mode::Swing && in_)
            s += in_->cv2 * 25.f;
        return s < 50.f ? 50.f : (s > 75.f ? 75.f : s);
    }
    double InternalStepSamples() const {
        if(fallback_period_ > 0 && settings.tempo == fallback_tempo_)
            return fallback_period_;
        const double bpm = settings.tempo < 30.f ? 30.0 : (settings.tempo > 300.f ? 300.0 : settings.tempo);
        return double(kSampleRate) * 60.0 / (bpm * 4.0); // a sixteenth
    }
    // Duration of the step with play index k: swing pairs 2T*s, then 2T*(1-s) (4.3).
    double StepSamples(uint32_t k) const {
        const double t = StepPeriodSamples(), s = Swing() / 100.0;
        return (k & 1) ? 2.0 * t * (1.0 - s) : 2.0 * t * s;
    }
    static uint32_t TicksOf(double samples) {
        const double t = samples / kBlock;
        return t <= 0 ? 0 : uint32_t(t + 0.5);
    }
    bool ClockOutActive() const {
        return settings.clock_out && state_ == State::Running &&
               (active_ == Active::Internal || active_ == Active::Gate);
    }

    // ----- notes and gates -----
    void DoStep() {
        if(pattern_queued_)
            ApplyQueuedPattern();
        if(active_ == Active::Internal && settings.tempo != fallback_tempo_)
            fallback_period_ = 0; // VR5 moved after an Auto fallback: back to the set tempo
        const double duration = StepSamples(step_count_);
        if(active_ == Active::Internal)
            next_ += uint64_t(duration * 4294967296.0);
        last_boundary_ = now_;
        ++step_count_;
        const int position = playhead_.Next(settings.mode, settings.length, rng_);
        const Step& step = pattern[std::size_t(position - 1)];
        out_->step_changed = true;
        ScheduleClocks(duration);
        if(!step.gate) {
            if(gate_high_)
                GateOff(); // slide into a step without gate: the gate falls here
            holding_ = false;
            return;
        }
        const int transpose = settings.transpose + (settings.cv1_mode == Cv1Mode::Transpose ? in_->cv1_semitones : 0);
        const uint8_t note = StepMidiNote(step, settings.scale, transpose, settings.cv_low_note, settings.cv_high_note);
        const uint8_t velocity = step.accent ? settings.accent_velocity : settings.velocity;
        int percent = step.gate_percent;
        if(settings.cv2_mode == Cv2Mode::GateLength)
            percent += int(std::lround(in_->cv2 * 95.f));
        percent = percent < kMinGatePercent ? kMinGatePercent : (percent > kMaxGatePercent ? kMaxGatePercent : percent);
        uint32_t length = TicksOf(duration * percent / 100.0);
        if(length < kMinGateHighTicks)
            length = kMinGateHighTicks;
        if(gate_high_ && holding_ && source_ == Source::Sequencer) {
            // Slide: no new envelope, the gate stays high, MIDI legato.
            if(note != note_) {
                OutEvent e;
                e.kind = OutEvent::Kind::Legato;
                e.note = note;
                e.prev_note = note_;
                e.velocity = velocity;
                e.accent = step.accent;
                e.prev_tag = tag_;
                e.tag = ++next_tag_;
                tag_ = e.tag;
                note_ = note;
                out_->Push(e);
                out_->glide = true;
            }
            pitch_ = note;
            holding_ = step.slide;
            gate_off_at_ = step.slide ? kNever : now_ + length;
            return;
        }
        next_note_ = note;
        next_velocity_ = velocity;
        next_accent_ = step.accent;
        next_source_ = Source::Sequencer;
        holding_ = step.slide;
        if(gate_high_)
            GateOff();
        if(gate_fell_) {
            // Retrigger gap: never gate-on in the tick of a gate-off (3.3).
            pending_on_ = true;
            gate_off_at_ = step.slide ? kNever : now_ + 1 + length;
        } else {
            GateOn();
            gate_off_at_ = step.slide ? kNever : now_ + length;
        }
    }
    void GateOn() {
        OutEvent e;
        e.kind = OutEvent::Kind::NoteOn;
        e.note = next_note_;
        e.velocity = next_velocity_;
        e.accent = next_accent_;
        e.source = next_source_;
        e.tag = ++next_tag_;
        tag_ = e.tag;
        note_ = next_note_;
        source_ = next_source_;
        pitch_ = next_note_;
        gate_high_ = true;
        out_->Push(e);
    }
    void GateOff() {
        if(!gate_high_)
            return;
        OutEvent e;
        e.kind = OutEvent::Kind::NoteOff;
        e.note = note_;
        e.source = source_;
        e.tag = tag_;
        out_->Push(e);
        gate_high_ = false;
        gate_fell_ = true;
        gate_off_at_ = kNever;
    }
    void StartPreview(int index) {
        const Step& step = pattern[std::size_t(index)];
        const int transpose = settings.transpose + (settings.cv1_mode == Cv1Mode::Transpose ? in_->cv1_semitones : 0);
        next_note_ = StepMidiNote(step, settings.scale, transpose, settings.cv_low_note, settings.cv_high_note);
        next_velocity_ = step.accent ? settings.accent_velocity : settings.velocity;
        next_accent_ = step.accent;
        next_source_ = Source::Preview;
        holding_ = false;
        if(gate_high_)
            GateOff(); // one preview slot: a new note ends the old one first
        if(gate_fell_ || pending_on_) {
            pending_on_ = true;
            gate_off_at_ = now_ + 1 + kPreviewTicks;
        } else {
            GateOn();
            gate_off_at_ = now_ + kPreviewTicks;
        }
    }
    // Start aborts a preview before the first step (4.1).
    void EndPreview() {
        preview_request_ = -1;
        if(pending_on_ && next_source_ == Source::Preview)
            pending_on_ = false;
        if(gate_high_ && source_ == Source::Preview) {
            if(out_)
                GateOff();
            else
                end_preview_ = true;
        }
    }
    void DoPanic() {
        pending_on_ = false;
        holding_ = false;
        GateOff();
        OutEvent e;
        e.kind = OutEvent::Kind::Panic;
        out_->Push(e);
        clocks_left_ = 0;
    }
    void ApplyQueuedPattern() {
        pattern = queued_pattern_;
        applied_revision_ = queued_revision_;
        pattern_queued_ = false;
    }

    // ----- MIDI clock out (24 PPQN: 6 per step) -----
    void ScheduleClocks(double duration) {
        if(!ClockOutActive()) {
            clocks_left_ = 0;
            return;
        }
        while(clocks_left_ > 0)
            EmitClock(); // an early step: the rest of the previous step now
        clock_start_ = now_;
        clock_span_ = duration / kBlock;
        clock_index_ = 0;
        clocks_left_ = 6;
    }
    void EmitClocks() {
        if(clocks_left_ > 0 && !ClockOutActive()) {
            clocks_left_ = 0;
            return;
        }
        while(clocks_left_ > 0 && now_ >= clock_start_ + uint32_t(clock_span_ * clock_index_ / 6.0 + 0.5))
            EmitClock();
    }
    void EmitClock() {
        OutEvent e;
        e.kind = OutEvent::Kind::Clock;
        out_->Push(e);
        ++clock_index_;
        --clocks_left_;
    }
    // Start/Stop/Continue out; received MIDI clock is never sent on (4.3).
    void EmitRt(OutEvent::Kind kind) {
        if(!settings.clock_out || settings.clock_source == ClockSource::Midi)
            return;
        if(!out_) {
            if(pending_rt_count_ < 4)
                pending_rt_[pending_rt_count_++] = kind;
            return;
        }
        OutEvent e;
        e.kind = kind;
        out_->Push(e);
    }
    void FlushPendingRt() {
        for(int i = 0; i < pending_rt_count_; ++i) {
            OutEvent e;
            e.kind = pending_rt_[i];
            out_->Push(e);
        }
        pending_rt_count_ = 0;
    }

    Xorshift32 rng_;
    Playhead playhead_;
    State state_ = State::Stop;
    ClockSource enabled_source_ = ClockSource::Internal; // clock source at enable
    Active active_ = Active::None, midi_port_ = Active::None, pending_source_ = Active::None;
    bool enabled_ = false, timed_out_ = false, await_clock_ = false, external_boundary_ = false;
    bool start_now_ = false;
    uint32_t now_ = 0, last_pulse_ = 0, prev_pulse_ = kNever, pulse_interval_ = 0;
    uint32_t scheduled_ = kNever, last_boundary_ = 0;
    uint32_t step_count_ = 0;
    int pulse_count_ = 0;
    uint64_t pos_ = 0, next_ = 0;
    double history_[4] = {};
    int history_pos_ = 0, history_count_ = 0;
    double period_est_ = 6000.0; // samples per step
    double fallback_period_ = 0;
    float fallback_tempo_ = -1.f;
    // notes and gate
    bool gate_high_ = false, gate_fell_ = false, holding_ = false, pending_on_ = false;
    bool end_preview_ = false, panic_request_ = false;
    uint32_t gate_off_at_ = kNever, tag_ = 0, next_tag_ = 0;
    uint8_t note_ = 48, pitch_ = 48, next_note_ = 48, next_velocity_ = 100;
    bool next_accent_ = false;
    Source source_ = Source::Sequencer, next_source_ = Source::Sequencer;
    int preview_request_ = -1;
    // pattern hand-over
    Pattern queued_pattern_{};
    uint16_t queued_revision_ = 0, applied_revision_ = 0;
    bool pattern_queued_ = false;
    // clock out
    uint32_t clock_start_ = 0, clock_index_ = 0;
    int clocks_left_ = 0;
    double clock_span_ = 0;
    OutEvent::Kind pending_rt_[4] = {};
    int pending_rt_count_ = 0;
    const TickInput* in_ = nullptr;
    TickOutput* out_ = nullptr;
};

} // namespace step_seq
