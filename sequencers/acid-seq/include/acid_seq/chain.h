#pragma once
// SPDX-License-Identifier: MIT
// Acid Seq signal path (ARBEITSAUFTRAG 4.10): sequencer events -> Acid 303 voice -> Drive
// (10 ms on/off fade) -> Stereo Delay (feed, tail, dry/wet in the host) -> soft limiter ->
// output. Header-only, no hardware; the firmware (src/main.cpp) and the end-to-end golden
// test run exactly this code. One Process() per 48-frame block after the sequencer tick.
#include "acid303/params.h"
#include "acid303/voice.h"
#include "drive/host.h"
#include "drive/params.h"
#include "step_seq/engine.h"
#include "stereo_delay/host.h"
#include "stereo_delay/params.h"

namespace acid_seq {

constexpr std::size_t kBlock = 48;

// Final output protection of the host: transparent below 0.8, smooth up to +-1.
inline float SoftLimit(float x) {
    const float a = x < 0.f ? -x : x;
    if(a <= 0.8f)
        return x;
    const float over = (a - 0.8f) / 0.2f;                      // 0 at the knee
    const float y = 0.8f + 0.2f * over / (1.f + over);         // approaches 1.0
    return x < 0.f ? -y : y;
}

class Chain {
  public:
    acid303::Params voice_params;
    ::seed::fx::drive::Parameters drive_params;
    ::seed::fx::delay::Parameters delay_params;

    // Startup only: the delay lines (4.6 MB) come from the host (SDRAM on the kit).
    bool Prepare(::seed::fx::StereoDelay::Storage& delay_memory) {
        voice_.SetParams(voice_params);
        voice_.Prepare();
        drive_mixer_.Reset(drive_params.enabled);
        delay_host_.Reset(delay_params.enabled);
        return drive_.Prepare({48000, kBlock}, drive_storage_) && delay_.Prepare({48000, kBlock}, delay_memory);
    }

    // Sequencer and preview notes of one tick drive the voice (TRANSPORT.md § 3/4).
    void Events(const step_seq::TickOutput& out) {
        for(int i = 0; i < out.count; ++i) {
            const step_seq::OutEvent& e = out.events[i];
            const acid303::Source source = e.source == step_seq::Source::Preview ? acid303::Source::Preview
                                                                                 : acid303::Source::Sequencer;
            switch(e.kind) {
                case step_seq::OutEvent::Kind::NoteOn:
                    voice_.NoteOn(e.note, e.velocity, e.accent, false, source, e.tag);
                    break;
                case step_seq::OutEvent::Kind::Legato:
                    voice_.NoteOn(e.note, e.velocity, e.accent, true, source, e.tag);
                    voice_.NoteOff(e.prev_tag, source);
                    break;
                case step_seq::OutEvent::Kind::NoteOff: voice_.NoteOff(e.tag, source); break;
                case step_seq::OutEvent::Kind::Panic: // sequencer and preview notes only (R16)
                    voice_.AllNotesOff(acid303::Source::Sequencer);
                    break;
                default: break;
            }
        }
    }
    // External MIDI notes on the voice channel share the voice (R16).
    void MidiNoteOn(uint8_t note, uint8_t velocity, uint32_t tag) {
        voice_.NoteOn(note, velocity, velocity > 100, false, acid303::Source::MidiIn, tag);
    }
    void MidiNoteOff(uint8_t note) { voice_.NoteOffNote(note, acid303::Source::MidiIn); }
    void MidiAllOff() { voice_.AllNotesOff(acid303::Source::MidiIn); }
    void Mute() { voice_.Mute(); }
    // The sequencer's tempo for synced delay times (Phase 6: tempo hand-over).
    void SetTempo(float bpm) { delay_.SetTempo(48000.f * 60.f / (bpm < 1.f ? 1.f : bpm)); }

    void Process(float* out_l, float* out_r, std::size_t n) {
        if(n > kBlock)
            n = kBlock;
        voice_.SetParams(voice_params);
        drive_.SetParameters(drive_params);
        drive_mixer_.SetEnabled(drive_params.enabled);
        delay_.SetParameters(delay_params);
        delay_host_.SetEnabled(delay_params.enabled);
        // Once the tail has ended the product is exactly dry: the dry/wet fades to 0 % (10 ms)
        // instead of keeping the dry part at 1 - mix as the standalone delay-fx does.
        const bool silent = !delay_params.enabled && delay_host_.GetState() == ::seed::fx::DelayHost::State::Off;
        delay_host_.SetMix(silent ? 0.f : delay_params.mix);
        // drv_l/drv_r are zeroed only because x86 GCC 13 -O2 reports a false
        // -Wmaybe-uninitialized for them; DriveMixer::Process writes all n samples.
        float voice[kBlock], wet_l[kBlock], wet_r[kBlock], drv_l[kBlock] = {}, drv_r[kBlock] = {};
        voice_.ProcessBlock(voice, n);
        if(!drive_.Process({voice, voice, wet_l, wet_r, n}))
            for(std::size_t i = 0; i < n; ++i)
                wet_l[i] = wet_r[i] = 0.f;
        drive_mixer_.Process(voice, voice, wet_l, wet_r, drv_l, drv_r, n);
        delay_host_.Process(drv_l, drv_r, out_l, out_r, n);
        for(std::size_t i = 0; i < n; ++i) {
            out_l[i] = SoftLimit(out_l[i]);
            out_r[i] = SoftLimit(out_r[i]);
        }
    }

    const acid303::Voice& GetVoice() const { return voice_; }
    const ::seed::fx::StereoDelay& GetDelay() const { return delay_; }
    const ::seed::fx::DelayHost& GetDelayHost() const { return delay_host_; }
    const ::seed::fx::DriveMixer& GetDriveMixer() const { return drive_mixer_; }

  private:
    acid303::Voice voice_;
    ::seed::fx::Drive drive_;
    ::seed::fx::Drive::Storage drive_storage_;
    ::seed::fx::DriveMixer drive_mixer_;
    ::seed::fx::StereoDelay delay_;
    ::seed::fx::DelayHost delay_host_{delay_};
};

} // namespace acid_seq
