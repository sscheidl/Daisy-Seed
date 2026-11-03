#pragma once
// SPDX-License-Identifier: MIT
// Seed3 Desktop Kit operation of Streichwurst (docs/CONTROLS.md), host-tested.
//
//  * Three pot pages (main / solo / FX), S9 cycles them. Soft takeover: after a page
//    change, a preset or a change from MIDI/app, a pot writes only after it has crossed
//    the stored value (or come within kPickup of it); afterwards it follows with a small
//    movement hysteresis against pot noise. Latest change wins (pot, MIDI or app).
//  * S1..S8 play C4..C5 (C major) on the kit's MIDI channel; S10 octaves, S11 ensemble
//    mode, S12 ensemble on/off, S13 solo sustain mode, S14 layer/split, S15 next RAM
//    preset, S16 panic (All Sound Off).
//  * S9 and S16 act on release, and only if the other one was not pressed meanwhile:
//    S9+S16 is the shared 5 s bootloader chord (common/bootloader) and must not change
//    the page or trigger a panic, whether held to the end, released early or aborted.
//  * LEDs: S1..S8 show held local keys; S9..S11 the active page (one lit, brief blink
//    after a change); S12 ensemble on; S13..S14 their states (dim/bright); S15 blinks
//    the preset number after a recall; S16 lit while a panic ramps. The LED frame is
//    rendered by the main loop (I2C), never in the audio callback.
#include "../../../common/bootloader/bootloader_entry.h"
#include "register.h"
#include "streichwurst/events.h"
#include <cstdint>

namespace streichwurst::product {

constexpr int kPots = 8, kKeys = 16, kPages = 3;
constexpr float kPickup = 0.02f, kPotHysteresis = 0.004f;
constexpr uint8_t kLocalNotes[8] = {60, 62, 64, 65, 67, 69, 71, 72};

struct PotTarget {
    uint8_t component;
    uint16_t id;
};
// CONTROLS.md pages: [page][pot].
constexpr PotTarget kPages_[kPages][kPots] = {
    {{kInstrument, id::Registration}, {kInstrument, id::Crescendo}, {kInstrument, id::Release}, {kProduct, id::Balance},
     {kFx, id::Animate}, {kFx, id::Phaser}, {kFx, id::Reverb}, {kProduct, id::Master}},
    {{kInstrument, id::SoloTone}, {kInstrument, id::SoloAttack}, {kInstrument, id::SoloRelease},
     {kInstrument, id::SoloTremDepth}, {kInstrument, id::SoloTremRate}, {kInstrument, id::SoloSustain},
     {kInstrument, id::SplitKey}, {kProduct, id::Master}},
    {{kFx, id::Animate}, {kFx, id::AnimateRate}, {kFx, id::Phaser}, {kFx, id::PhaserRate}, {kFx, id::Reverb},
     {kFx, id::ReverbDecay}, {kProduct, id::Master}, {kProduct, id::Balance}},
};

struct ControlFrame {
    float pots[kPots]{};
    uint16_t keys = 0; // debounced held keys, bit 0 = S1
    uint32_t ms = 0;
};

// What one control tick produced for the callback.
struct ControlOutput {
    Event events[16];
    int event_count = 0;
    bool panic = false;
    bool bootloader = false;
    bool state_changed = false;
};

class Controls {
  public:
    // A parameter changed from somewhere else (MIDI, app, preset): pots must pick it up.
    void Latch(uint8_t component, uint16_t pid) {
        for(int p = 0; p < kPots; ++p) {
            const PotTarget& t = kPages_[page_][p];
            if(t.component == component && t.id == pid)
                latched_[p] = true;
        }
    }
    void LatchAll() {
        for(bool& l : latched_)
            l = true;
    }
    int Page() const { return page_; }
    bool Latched(int pot) const { return latched_[pot]; }

    // One control tick (1 kHz). Writes pot and key changes into `s`.
    ControlOutput Process(const ControlFrame& f, State& s) {
        ControlOutput out;
        if(first_) {
            first_ = false;
            for(int p = 0; p < kPots; ++p)
                last_pot_[p] = f.pots[p];
            LatchAll(); // power-up: the defaults hold until each pot is moved across them
            keys_ = f.keys;
            Leds(f, s);
            return out;
        }
        Pots(f, s, out);
        Keys(f, s, out);
        if(combo_.Process(f.keys, f.ms))
            out.bootloader = true;
        Leds(f, s);
        return out;
    }
    uint32_t LedFrame() const { return leds_; }
    void SetPanicActive(bool on) { panic_active_ = on; }

  private:
    void Pots(const ControlFrame& f, State& s, ControlOutput& out) {
        for(int p = 0; p < kPots; ++p) {
            const PotTarget& t = kPages_[page_][p];
            const sq::ParamDesc* d = sq::FindParam(Table(t.component), t.id);
            int32_t current = 0;
            if(!d || !ReadParam(s, t.component, t.id, current))
                continue;
            const float value = Normalized(*d, current);
            const float pot = f.pots[p] < 0.f ? 0.f : (f.pots[p] > 1.f ? 1.f : f.pots[p]);
            if(latched_[p]) {
                const bool crossed = (last_pot_[p] - value) * (pot - value) <= 0.f && last_pot_[p] != pot;
                if(std::fabs(pot - value) < kPickup || crossed) {
                    latched_[p] = false;
                    moved_[p] = pot;
                }
                last_pot_[p] = pot;
                continue;
            }
            last_pot_[p] = pot;
            if(std::fabs(pot - moved_[p]) < kPotHysteresis)
                continue;
            moved_[p] = pot;
            const int32_t v = FromNormalized(*d, pot);
            if(v != current && WriteParam(s, t.component, t.id, v)) {
                out.state_changed = true;
                LatchOthers(p, t);
            }
        }
    }
    // Should a page carry the same parameter on two pots, the other one is latched so it
    // cannot jump back.
    void LatchOthers(int pot, const PotTarget& t) {
        for(int q = 0; q < kPots; ++q)
            if(q != pot && kPages_[page_][q].component == t.component && kPages_[page_][q].id == t.id)
                latched_[q] = true;
    }
    void Toggle(State& s, uint8_t component, uint16_t pid, int count, ControlOutput& out) {
        int32_t v = 0;
        ReadParam(s, component, pid, v);
        const int next = (v / 1000 + 1) % count;
        WriteParam(s, component, pid, next * 1000, true);
        out.state_changed = true;
    }
    void Keys(const ControlFrame& f, State& s, ControlOutput& out) {
        const uint16_t pressed = uint16_t(f.keys & ~keys_), released = uint16_t(keys_ & ~f.keys);
        keys_ = f.keys;
        const uint8_t ch = uint8_t(s.prod.midi_channel > 0 ? s.prod.midi_channel - 1 : 0);
        for(int k = 0; k < 8; ++k) {
            if((pressed >> k) & 1u)
                Push(out, NoteOn(ch, kLocalNotes[k], 100));
            if((released >> k) & 1u)
                Push(out, NoteOff(ch, kLocalNotes[k]));
        }
        // S9 (bit 8) and S16 (bit 15): delayed to release; a chord cancels both.
        constexpr uint16_t kS9 = 1u << 8, kS16 = 1u << 15;
        if(pressed & kS9)
            s9_armed_ = true;
        if(pressed & kS16)
            s16_armed_ = true;
        if((f.keys & kS9) && (f.keys & kS16))
            s9_armed_ = s16_armed_ = false; // chord started: neither single action
        if((released & kS9) && s9_armed_) {
            s9_armed_ = false;
            page_ = (page_ + 1) % kPages;
            s.pot_page = page_;
            out.state_changed = true;
            LatchAll();
            page_blink_until_ = f.ms + 400;
        }
        if((released & kS16) && s16_armed_) {
            s16_armed_ = false;
            out.panic = true;
        }
        if(pressed & (1u << 9))
            Toggle(s, kInstrument, id::Octave, 3, out);
        if(pressed & (1u << 10))
            Toggle(s, kFx, id::EnsembleMode, 3, out);
        if(pressed & (1u << 11))
            Toggle(s, kFx, id::EnsembleOn, 2, out);
        if(pressed & (1u << 12))
            Toggle(s, kInstrument, id::SoloSustainMode, 3, out);
        if(pressed & (1u << 13))
            Toggle(s, kInstrument, id::SplitMode, 2, out);
        if(pressed & (1u << 14)) {
            const int next = (s.preset + 1) & 3;
            const ProductParams keep = s.prod; // channel, balance and master stay with the kit
            const int page = s.pot_page;
            s = Preset(next);
            s.prod = keep;
            s.pot_page = page;
            out.state_changed = true;
            LatchAll();
            preset_blink_until_ = f.ms + 250u * uint32_t(next + 1) * 2u;
            preset_blink_start_ = f.ms;
        }
    }
    static void Push(ControlOutput& out, const Event& e) {
        if(out.event_count < 16)
            out.events[out.event_count++] = e;
    }
    // 2 bits per LED: off, dim, mid, bright.
    void Leds(const ControlFrame& f, const State& s) {
        uint32_t leds = 0;
        auto set = [&](int i, uint32_t level) { leds |= level << (2 * i); };
        for(int k = 0; k < 8; ++k)
            if((f.keys >> k) & 1u)
                set(k, 3);
        const bool blink = f.ms < page_blink_until_ && ((f.ms / 100) & 1u);
        set(8 + page_, blink ? 1 : 3);
        set(11, s.fx.ensemble_on ? 3 : 0);
        set(12, s.inst.solo_sustain_mode == 0 ? 0 : (s.inst.solo_sustain_mode == 1 ? 1 : 3));
        set(13, s.inst.split_mode ? 3 : 0);
        if(f.ms < preset_blink_until_)
            set(14, ((f.ms - preset_blink_start_) / 250u) & 1u ? 0 : 3); // n+1 flashes
        set(15, panic_active_ ? 3 : 0);
        leds_ = leds;
    }

    bool first_ = true;
    int page_ = 0;
    float last_pot_[kPots]{}, moved_[kPots]{};
    bool latched_[kPots]{};
    uint16_t keys_ = 0;
    bool s9_armed_ = false, s16_armed_ = false;
    bootloader::KeyCombo combo_;
    uint32_t leds_ = 0, page_blink_until_ = 0, preset_blink_until_ = 0, preset_blink_start_ = 0;
    bool panic_active_ = false;
};

} // namespace streichwurst::product
