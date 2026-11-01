#pragma once
// SPDX-License-Identifier: MIT
// Streichwurst instrument component (MODULAR_ARCHITECTURE.md): string section and soli.
//
// Contract (version STREICHWURST_INSTRUMENT_VERSION):
//  * Prepare(sample_rate, max_block, storage) once outside the audio callback; Reset() any
//    time. The host owns and places the key tables (InstrumentStorage, 6 KiB), which are
//    touched only by events; the firmware puts them outside the tightly coupled RAM.
//  * SetParameters(p): sanitized copy, applied at the next sample (smoothing inside the
//    sections); SetAnimateOffset()/SetPitchModulation(): typed control inputs.
//  * RenderSections(events, count, buses): two separate mono float buses (strings, soli)
//    with the same frame count and time base; strings != solo (no aliasing), frames <=
//    max_block, otherwise the call is rejected before touching any buffer. Events must
//    carry frame < frames; equal frames keep their input order, a decreasing frame is
//    treated as the previous frame, a frame >= frames as the last frame (both counted).
//    Events split the block sample-accurately; envelopes and control updates run on a
//    16-sample grid that continues across host blocks.
//  * frames == 0: events are kept (up to kPendingEvents) and applied at frame 0 of the
//    next non-empty block; phases do not advance. A full pending queue drops Note-Ons
//    (counted) and turns any lost note release into an AllNotesOff for the next block,
//    so an overflow never leaves a hanging note.
// The instrument owns no MIDI parser, no FX and no board code.
#include "events.h"
#include "params.h"
#include "solo_section.h"
#include "string_section.h"
#include "version.h"
#include <cstddef>
#include <cstring>

namespace streichwurst {

constexpr int kChannels = 16;

struct SectionBuses {
    float* strings = nullptr;
    float* solo = nullptr;
    std::size_t frames = 0;
};

// Per channel and key: held count, routing bits and the transposed note fixed at Note-On.
struct InstrumentStorage {
    uint8_t held[kChannels][kNotes];
    uint8_t route[kChannels][kNotes];
    uint8_t mapped[kChannels][kNotes];
};

struct InstrumentStats {
    uint32_t invalid_events = 0;    // channel/note out of range
    uint32_t reordered_events = 0;  // frame decreasing or beyond the block, clamped
    uint32_t dropped_note_ons = 0;  // pending queue full
    uint32_t forced_all_off = 0;    // releases lost in a full pending queue
    uint32_t rejected_blocks = 0;   // invalid buses
};

class Instrument {
  public:
    static constexpr std::size_t kPendingEvents = 64;
    // Fixed headroom of the two section buses (own; docs/FIDELITY.md, gain plan).
    static constexpr float kStringGain = 0.25f, kSoloGain = 0.5f;

    bool Prepare(float sample_rate, std::size_t max_block, InstrumentStorage& storage) {
        if(max_block == 0 || !strings_.Prepare(sample_rate) || !solo_.Prepare(sample_rate))
            return false;
        held_ = storage.held;
        route_ = storage.route;
        mapped_ = storage.mapped;
        max_block_ = max_block;
        prepared_ = true;
        Reset();
        return true;
    }
    void Reset() {
        strings_.Reset();
        solo_.Reset();
        std::memset(held_, 0, sizeof(InstrumentStorage::held));
        std::memset(route_, 0, sizeof(InstrumentStorage::route));
        std::memset(mapped_, 0, sizeof(InstrumentStorage::mapped));
        std::memset(velocity_, 0, sizeof(velocity_));
        std::memset(string_count_, 0, sizeof(string_count_));
        std::memset(pedal_, 0, sizeof(pedal_));
        pending_count_ = 0;
        force_all_off_ = false;
        bend_ = 0.f;
        ApplyPitch();
    }

    void SetParameters(const InstrumentParams& in) {
        const InstrumentParams p = Sanitize(in, params_);
        if(std::memcmp(&p, &params_, sizeof(p)) == 0)
            return;
        const bool velocity_changed = p.string_velocity != params_.string_velocity;
        params_ = p;
        strings_.SetParameters(p.string_registration, p.string_octave, p.string_crescendo, p.string_release);
        solo_.SetParameters(p);
        ApplyPitch();
        if(velocity_changed)
            for(int m = 0; m < kNotes; ++m)
                RefreshString(m);
    }
    const InstrumentParams& Parameters() const { return params_; }
    void SetAnimateOffset(float native) { strings_.SetAnimateOffset(std::isfinite(native) ? native : 0.f); }
    // Extra pitch in semitones (e.g. a modulation matrix target), added to bend and tune.
    void SetPitchModulation(float semitones) {
        pitch_mod_ = std::isfinite(semitones) ? (semitones < -24.f ? -24.f : (semitones > 24.f ? 24.f : semitones)) : 0.f;
        ApplyPitch();
    }

    bool RenderSections(const Event* events, std::size_t count, const SectionBuses& out) {
        if(!prepared_ || out.frames > max_block_ || (count && !events) ||
           (out.frames && (!out.strings || !out.solo || out.strings == out.solo))) {
            ++stats_.rejected_blocks;
            return false;
        }
        if(out.frames == 0) {
            for(std::size_t i = 0; i < count; ++i)
                Defer(events[i]);
            return true;
        }
        std::size_t frame = 0;
        if(force_all_off_) {
            force_all_off_ = false;
            AllNotesOffNow();
        }
        for(std::size_t i = 0; i < pending_count_; ++i)
            ApplyEvent(pending_[i]);
        pending_count_ = 0;
        uint32_t last_frame = 0;
        for(std::size_t i = 0; i < count; ++i) {
            uint32_t at = events[i].frame;
            if(at < last_frame || at >= out.frames) {
                ++stats_.reordered_events;
                at = at >= out.frames ? uint32_t(out.frames - 1) : last_frame;
                if(at < last_frame)
                    at = last_frame;
            }
            last_frame = at;
            Render(out, frame, at);
            frame = at;
            ApplyEvent(events[i]);
        }
        Render(out, frame, out.frames);
        return true;
    }

    // Immediate application (the render loop calls it at the event's frame).
    void ApplyEvent(const Event& e) {
        if(e.channel >= kChannels || e.note >= kNotes) {
            ++stats_.invalid_events;
            return;
        }
        switch(e.type) {
            case EventType::NoteOn:
                if(e.value == 0)
                    Release(e.channel, e.note);
                else
                    Press(e.channel, e.note, e.value);
                break;
            case EventType::NoteOff: Release(e.channel, e.note); break;
            case EventType::Sustain: Pedal(e.channel, e.value >= 64); break;
            case EventType::PitchBend: {
                const int b = e.bend < -8192 ? -8192 : (e.bend > 8191 ? 8191 : e.bend);
                bend_ = float(b) / 8192.f;
                ApplyPitch();
                break;
            }
            case EventType::AllNotesOff: AllNotesOffNow(); break;
            case EventType::AllSoundOff:
                AllNotesOffNow();
                strings_.Silence();
                solo_.Silence();
                break;
        }
    }

    const InstrumentStats& Stats() const { return stats_; }
    const StringSection& Strings() const { return strings_; }
    const SoloSection& Solo() const { return solo_; }
    std::size_t Pending() const { return pending_count_; }
    bool StringNoteActive(int m) const { return m >= 0 && m < kNotes && string_count_[m] > 0; }

  private:
    // route_ bits: target sections fixed at the first Note-On, and the pedal hold.
    enum Route : uint8_t { RouteStrings = 1, RouteSolo = 2, Sustained = 4 };
    bool IsSustained(uint8_t ch, uint8_t note) const { return route_[ch][note] & Sustained; }
    void SetSustained(uint8_t ch, uint8_t note, bool on) {
        route_[ch][note] = uint8_t(on ? route_[ch][note] | Sustained : route_[ch][note] & ~Sustained);
    }

    void Render(const SectionBuses& out, std::size_t from, std::size_t to) {
        for(std::size_t n = from; n < to; ++n) {
            out.strings[n] = strings_.Process() * kStringGain;
            out.solo[n] = solo_.Process() * kSoloGain;
        }
    }
    void Defer(const Event& e) {
        if(pending_count_ < kPendingEvents) {
            pending_[pending_count_++] = e;
            pending_[pending_count_ - 1].frame = 0;
            return;
        }
        const bool release = e.type == EventType::NoteOff || (e.type == EventType::NoteOn && e.value == 0) ||
                             e.type == EventType::Sustain || e.type == EventType::AllNotesOff ||
                             e.type == EventType::AllSoundOff;
        if(release) {
            force_all_off_ = true;
            ++stats_.forced_all_off;
        } else if(e.type == EventType::NoteOn) {
            ++stats_.dropped_note_ons;
        }
    }
    void ApplyPitch() {
        const float semis = bend_ * float(params_.bend_range) + params_.tune_cents / 100.f + pitch_mod_;
        strings_.SetPitch(semis);
        solo_.SetPitch(semis);
    }
    int Transposed(uint8_t note) const { return int(note) + params_.transpose; }

    void Press(uint8_t ch, uint8_t note, uint8_t velocity) {
        uint8_t& held = held_[ch][note];
        if(held == 0 && !IsSustained(ch, note)) {
            // Routing and transposition are fixed while the key sounds: later parameter
            // changes cannot strand a note in the other section.
            const int m = Transposed(note);
            uint8_t route = 0;
            if(m >= 0 && m < kNotes) {
                const bool solo_zone = int(note) >= params_.split_key;
                if(params_.split_mode == SplitLayer || !solo_zone)
                    route |= RouteStrings;
                if(params_.split_mode == SplitLayer || solo_zone)
                    route |= RouteSolo;
            }
            route_[ch][note] = route;
            mapped_[ch][note] = uint8_t(m < 0 ? 0 : (m > 127 ? 127 : m));
            if(route & RouteStrings) {
                ++string_count_[mapped_[ch][note]];
            }
        }
        SetSustained(ch, note, false);
        if(held < 255)
            ++held;
        const uint8_t m = mapped_[ch][note];
        if(route_[ch][note] & RouteStrings) {
            velocity_[m] = velocity;
            RefreshString(m);
        }
        if(route_[ch][note] & RouteSolo)
            solo_.NoteOn(ch, m, velocity);
    }
    void Release(uint8_t ch, uint8_t note) {
        uint8_t& held = held_[ch][note];
        if(held == 0)
            return;
        if(--held > 0)
            return; // a repeated key: released with its last Note-Off
        if(pedal_[ch]) {
            SetSustained(ch, note, true);
            solo_.NoteOff(ch, mapped_[ch][note], true);
            return;
        }
        Finish(ch, note);
    }
    void Finish(uint8_t ch, uint8_t note) {
        const uint8_t m = mapped_[ch][note];
        if(route_[ch][note] & RouteStrings) {
            if(string_count_[m])
                --string_count_[m];
            RefreshString(m);
        }
        if(route_[ch][note] & RouteSolo)
            solo_.NoteOff(ch, m, false);
        route_[ch][note] = 0;
    }
    void Pedal(uint8_t ch, bool down) {
        pedal_[ch] = down;
        if(down)
            return;
        for(int n = 0; n < kNotes; ++n)
            if(IsSustained(ch, uint8_t(n)) && held_[ch][n] == 0)
                Finish(ch, uint8_t(n));
        solo_.PedalUp(ch);
    }
    void AllNotesOffNow() {
        std::memset(held_, 0, sizeof(InstrumentStorage::held));
        std::memset(route_, 0, sizeof(InstrumentStorage::route));
        std::memset(string_count_, 0, sizeof(string_count_));
        strings_.ReleaseAll();
        solo_.ReleaseAll();
    }
    void RefreshString(int m) {
        float a = 0.f;
        if(string_count_[m])
            a = (1.f - params_.string_velocity) + params_.string_velocity * float(velocity_[m]) / 127.f;
        strings_.SetActivation(m, a);
    }

    bool prepared_ = false;
    std::size_t max_block_ = 0;
    InstrumentParams params_;
    StringSection strings_;
    SoloSection solo_;
    uint8_t (*held_)[kNotes] = nullptr;
    uint8_t (*route_)[kNotes] = nullptr;
    uint8_t (*mapped_)[kNotes] = nullptr;
    uint8_t velocity_[kNotes]{};
    uint16_t string_count_[kNotes]{};
    bool pedal_[kChannels]{};
    Event pending_[kPendingEvents];
    std::size_t pending_count_ = 0;
    bool force_all_off_ = false;
    float bend_ = 0.f, pitch_mod_ = 0.f;
    InstrumentStats stats_;
};

} // namespace streichwurst
