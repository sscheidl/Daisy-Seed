#pragma once
// MIT. Board-independent part of the toy-piano-filter test product: one filter per voice,
// the two control pages, and a load test that keeps a chosen number of voices sounding.
// No effects, so every cycle goes to voices and filters.
#include "toy_piano.h"
#include "../../../modules/filter-core/include/filter_core/ladder.h"
#include <array>
#include <cmath>

namespace toy_piano {
namespace filter = seed::dsp::filter;

constexpr float kCutoffLow = 24.f, kCutoffSpan = 111.f; // VR5: 33 Hz .. 19.9 kHz, linear in notes
constexpr float kEnvSpan = 60.f;                        // +- semitones at the ends of VR5, page 2
constexpr float kPickUpWindow = 0.02f;
constexpr int kSwitchUp = 1, kSwitchDown = 2;           // daisy::Switch3 positions
constexpr std::size_t kFilterCall = 16;                 // 3 kHz control rate for the cutoff
constexpr unsigned kTailBlocks = 60, kFadeBlocks = 20;  // filters ring on 60 ms after the envelope
constexpr unsigned kLoadSteps = 9;                      // VR7, page 2: 0, 4, 8 .. 32 voices

// What the two pages say. VR1-VR4 and VR8 are the same on both pages.
struct FilterControls {
    float pitch = 0.5f, wave = 0.f, attack = 0.f, decay = 0.5f, age = 0.f; // VR1-VR4, VR8
    float cutoff = 1.f, resonance = 0.f, drive = 0.f;                      // VR5-VR7, page 1
    float env = 0.f;                                                       // VR5, page 2: -1..1
    filter::Model model = filter::Model::Stages;                           // VR6, page 2
    bool shared = false;                                                   // VR6, page 2: one filter on the sum
    unsigned load_voices = 0;                                              // VR7, page 2
    filter::Mode mode = filter::Mode::Lp24;                                // SW18
    bool page2 = false;                                                    // SW17
};

class FilterPanel {
  public:
    // Control rate (once per block). A pot shared by the two pages takes over only once it
    // has reached the stored value, so nothing jumps when SW17 flips.
    void Update(const std::array<float, 8>& raw, bool sw17, int sw18) {
        float x[8];
        for(unsigned i = 0; i < 8; ++i)
            x[i] = pots_[i].Process(raw[i]);
        c_.pitch = x[0];
        c_.wave = x[1];
        c_.attack = x[2];
        c_.decay = x[3];
        c_.age = x[7];
        c_.mode = sw18 == kSwitchUp ? filter::Mode::Lp24 : (sw18 == kSwitchDown ? filter::Mode::Hp : filter::Mode::Bp);
        const unsigned page = sw17 ? 1 : 0;
        if(!started_) {
            for(unsigned i = 0; i < 3; ++i) { // the page in use starts at its pots
                value_[page][i] = x[4 + i];
                value_[1 - page][i] = kDefaults[1 - page][i];
            }
        } else if(page != page_) {
            for(bool& picked : picked_)
                picked = false;
        }
        for(unsigned i = 0; i < 3; ++i) {
            float& value = value_[page][i];
            const float before = last_[i] - value, now = x[4 + i] - value;
            if(!started_ || std::fabs(now) < kPickUpWindow || before * now <= 0.f)
                picked_[i] = true;
            if(picked_[i])
                value = x[4 + i];
            last_[i] = x[4 + i];
        }
        page_ = page;
        started_ = true;
        c_.page2 = page == 1;
        c_.cutoff = value_[0][0];
        c_.resonance = value_[0][1];
        c_.drive = value_[0][2];
        const float env = value_[1][0] * 2.f - 1.f; // centre detent
        c_.env = std::fabs(env) < 0.02f ? 0.f : env;
        const unsigned model = unsigned(value_[1][1] * 3.999f);
        c_.model = model == 0 ? filter::Model::Clean : (model == 1 ? filter::Model::Sum : filter::Model::Stages);
        c_.shared = model == 3;
        c_.load_voices = 4 * unsigned(value_[1][2] * (float(kLoadSteps) - 0.001f));
    }
    const FilterControls& Controls() const { return c_; }

  private:
    // Page 1: filter open, no resonance, no drive. Page 2: envelope off, Stages, no load.
    static constexpr float kDefaults[2][3] = {{1.f, 0.f, 0.f}, {0.5f, 0.6f, 0.f}};
    PotFilter pots_[8];
    float value_[2][3] = {}, last_[3] = {};
    bool picked_[3] = {};
    unsigned page_ = 0;
    bool started_ = false;
    FilterControls c_;
};

class FilterHost {
  public:
    void Prepare() {
        for(unsigned v = 0; v < kNumVoices; ++v) // every voice gets its own component tolerances
            filters_[v].Seed(0xA511E000u + v * 0x9E3779B1u);
        shared_.Seed(0x5A4ED000u);
    }
    void ReadControls(const std::array<float, 8>& raw, bool sw17, int sw18) { panel_.Update(raw, sw17, sw18); }
    const FilterControls& Controls() const { return panel_.Controls(); }
    // Keeps the first `Controls().load_voices` voices sounding, so that the load is steady.
    void HoldLoad(ToyPiano& piano) const {
        const unsigned voices = Controls().load_voices;
        for(unsigned v = 0; v < voices && v < kNumVoices; ++v) {
            if(piano.VoiceActive(v))
                continue;
            if(v < kNumKeys)
                piano.Trigger(v, kAttackMin, kDecayMax);
            else
                piano.NoteOn(uint8_t(48 + v - kNumKeys), 100, kAttackMin, kDecayMax);
        }
    }
    // Control rate: x values are 0..1 positions after the matrix. `cutoff` is per voice.
    void SetFilter(const std::array<float, kNumVoices>& cutoff, float resonance, float drive) {
        const FilterControls& c = Controls();
        filter::Parameters p;
        p.resonance = resonance;
        p.drive = drive;
        p.age = c.age;
        p.mode = c.mode;
        p.model = c.model;
        p.bass_comp = 1.f;
        if(p.resonance != shared_parameters_.resonance || p.drive != shared_parameters_.drive ||
           p.age != shared_parameters_.age || p.mode != shared_parameters_.mode || p.model != shared_parameters_.model) {
            shared_parameters_ = p;
            ++revision_;
        }
        for(unsigned v = 0; v < kNumVoices; ++v)
            cutoff_[v] = kCutoffLow + kCutoffSpan * cutoff[v];
    }

    void Process(ToyPiano& piano, float* mono, std::size_t frames) {
        const FilterControls& c = Controls();
        float sum[kBlockSize] = {}, voice[kBlockSize];
        for(unsigned v = 0; v < kNumVoices; ++v) {
            const bool sounding = piano.VoiceActive(v);
            if(sounding) {
                if(!tail_[v]) {
                    filters_[v].Reset();
                    applied_[v] = revision_ - 1; // Reset: take the parameters afresh
                }
                tail_[v] = kTailBlocks;
            } else if(!tail_[v]) {
                continue;
            } else {
                --tail_[v];
            }
            if(sounding)
                piano.RenderVoice(v, c.wave, voice, frames);
            else
                for(std::size_t i = 0; i < frames; ++i)
                    voice[i] = 0.f;
            if(!c.shared)
                FilterVoice(v, piano.VoiceEnvelope(v) * c.env * kEnvSpan, voice, frames);
            const float fade = tail_[v] < kFadeBlocks ? float(tail_[v]) * (1.f / float(kFadeBlocks)) : 1.f;
            for(std::size_t i = 0; i < frames; ++i)
                sum[i] += voice[i] * fade;
        }
        if(c.shared) { // comparison only: one filter on the sum, so no envelope per note
            filter::Parameters p = shared_parameters_;
            p.cutoff_note = cutoff_[0];
            shared_.Set(p);
            for(std::size_t i = 0; i < frames; ++i)
                sum[i] *= kVoiceGain * kNumKeys * 0.25f; // a mix level the saturators can work with
            shared_.Process(sum, sum, frames);
            for(std::size_t i = 0; i < frames; ++i)
                mono[i] = ToyPiano::SoftLimit(sum[i] * (1.f / (kNumKeys * 0.25f)));
            return;
        }
        for(std::size_t i = 0; i < frames; ++i)
            mono[i] = ToyPiano::SoftLimit(sum[i] * kVoiceGain);
    }

    unsigned ActiveFilters() const {
        unsigned count = 0;
        for(unsigned tail : tail_)
            count += tail != 0;
        return count;
    }
    const filter::Ladder& VoiceFilter(unsigned v) const { return filters_[v]; }
    float VoiceCutoff(unsigned v) const { return note_[v]; }

  private:
    void FilterVoice(unsigned v, float envelope_notes, float* voice, std::size_t frames) {
        filter::Ladder& f = filters_[v];
        if(applied_[v] != revision_) {
            filter::Parameters p = shared_parameters_;
            p.cutoff_note = cutoff_[v] + envelope_notes;
            f.Set(p);
            note_[v] = p.cutoff_note;
            applied_[v] = revision_;
        }
        // The per-voice cutoff input: matrix and envelope add up here in note units, and any
        // later source (an ADSR) can join them. It ramps in calls of kFilterCall samples.
        const float target = cutoff_[v] + envelope_notes;
        const float step = (target - note_[v]) * (float(kFilterCall) / float(frames));
        for(std::size_t i = 0; i < frames; i += kFilterCall) {
            note_[v] += step;
            f.SetCutoff(note_[v]);
            f.ProcessHeld(voice + i, voice + i, kFilterCall);
        }
        note_[v] = target;
    }

    FilterPanel panel_;
    filter::Ladder filters_[kNumVoices];
    filter::Ladder shared_;
    filter::Parameters shared_parameters_;
    std::array<float, kNumVoices> cutoff_{}, note_{};
    std::array<uint32_t, kNumVoices> applied_{};
    std::array<unsigned, kNumVoices> tail_{};
    uint32_t revision_ = 1;
};
} // namespace toy_piano
