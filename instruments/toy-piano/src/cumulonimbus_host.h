#pragma once
// MIT. Board-independent part of Toy Piano on Cumulonimbus: the shared 16-entry parameter register,
// the SW17 Piano/FX pages with per-destination pickup, and the host that routes the mono piano sum
// through the shared GranularReverbChain (effects/cumulonimbus/src/chain.h). The chain owns grain
// and reverb mixing and the Freeze/Run/Bypass policy; this file owns only mapping and routing.
#include "toy_piano.h"
#include "../../../effects/cumulonimbus/src/diagnostics.h"
#include "../../../effects/cumulonimbus/src/fx_panel.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace toy_piano {

// Destinations of the register: Piano page VR1..VR8, then FX page VR1..VR8.
enum Dest : uint8_t {
    kDestAttack, kDestDecay, kDestTranspose, kDestHarmonic, kDestLevel, kDestLfo1Rate, kDestLfo2Rate,
    kDestRandomRate,
    kDestPosition, kDestGrainLength, kDestDensity, kDestGrainPitch, kDestGrainMix, kDestSpread,
    kDestHallDecay, kDestHallMix,
    kDestCount
};
enum Page : uint8_t { kPagePiano, kPageFx };
constexpr unsigned kPotsPerPage = 8;
constexpr float kCumuloPickUpWindow = 0.02f;

// Defaults: the Piano page maps exactly to the regular Toy Piano bases (transposition 1, level gain 1,
// harmonic 0.12); the rates are the Mod on Daisy patch defaults (6000, 6000, 8000 of kRateMax). The FX
// page starts low-wet (seed::fx::FxDefaultPots()).
inline std::array<float, kDestCount> DefaultRegister() {
    std::array<float, kDestCount> r{};
    r[kDestAttack] = 0.f;
    r[kDestDecay] = .5f;
    r[kDestTranspose] = .5f;
    r[kDestHarmonic] = kHarmonicBase;
    r[kDestLevel] = kLevelBase;
    r[kDestLfo1Rate] = 6000.f / 16383.f; // mod::kRateMax
    r[kDestLfo2Rate] = 6000.f / 16383.f;
    r[kDestRandomRate] = 8000.f / 16383.f;
    const auto fx = seed::fx::FxDefaultPots();
    for(unsigned i = 0; i < kPotsPerPage; ++i)
        r[kDestPosition + i] = fx[i];
    return r;
}

// Transposition: +-24 semitones, exactly 1 in the centre detent (as Toy Piano 0.5 VR1).
inline float TransposeRatio(float x) { return std::fabs(x - .5f) < .005f ? 1.f : std::exp2((x - .5f) * 4.f); }

struct ExternalWrite {
    uint8_t dest;
    float value;
};

// Audio thread only. Pots go through PotFilter; a pot changes its destination only once it has
// reached the stored value (pickup), after a page change and after an external write.
class CumulonimbusPanel {
  public:
    void Reset() {
        value_ = DefaultRegister();
        started_ = false;
        page_ = kPagePiano;
        for(auto& p : picked_) p = false;
    }
    // Control rate (once per block).
    void Update(const std::array<float, 8>& raw, bool sw17, int sw18) {
        float x[8];
        for(unsigned i = 0; i < 8; ++i) x[i] = pots_[i].Process(raw[i]);
        const Page page = sw17 ? kPageFx : kPagePiano;
        if(!started_) {
            // The Piano page in use at boot starts at its pots; the FX page always starts at its safe defaults.
            if(page == kPagePiano)
                for(unsigned i = 0; i < kPotsPerPage; ++i) value_[i] = x[i];
            for(unsigned i = 0; i < kPotsPerPage; ++i) picked_[i] = page == kPagePiano;
        } else if(page != page_) {
            for(auto& p : picked_) p = false;
            ++page_changes_;
        }
        const unsigned base = page == kPagePiano ? 0u : kPotsPerPage;
        for(unsigned i = 0; i < kPotsPerPage; ++i) {
            float& value = value_[base + i];
            const float before = last_[i] - value, now = x[i] - value;
            if(started_ && (std::fabs(now) < kCumuloPickUpWindow || before * now <= 0.f))
                picked_[i] = true;
            if(picked_[i] && value != x[i]) {
                value = x[i];
                pot_writes_[base + i] = true;
            }
            last_[i] = x[i];
        }
        page_ = page;
        mode_ = seed::fx::ModeFromSwitch(sw18);
        started_ = true;
    }
    // An external write (MIDI CC, app). A pot on the visible page must pick up the new value.
    void Write(const ExternalWrite& w) {
        if(w.dest >= kDestCount || !std::isfinite(w.value))
            return;
        value_[w.dest] = w.value < 0.f ? 0.f : (w.value > 1.f ? 1.f : w.value);
        pot_writes_[w.dest] = false;
        if((w.dest < kPotsPerPage) == (page_ == kPagePiano))
            picked_[w.dest % kPotsPerPage] = false;
    }
    float Value(Dest d) const { return value_[d]; }
    const std::array<float, kDestCount>& Values() const { return value_; }
    Page CurrentPage() const { return page_; }
    seed::fx::ChainMode Mode() const { return mode_; }
    bool Picked(unsigned pot) const { return picked_[pot]; }
    // True once a pot (not an external write) has set this destination since the last call.
    bool TakePotWrite(Dest d) {
        const bool w = pot_writes_[d];
        pot_writes_[d] = false;
        return w;
    }
    unsigned PageChanges() const { return page_changes_; }

    seed::fx::FxControls Fx() const {
        std::array<float, 8> x{};
        for(unsigned i = 0; i < kPotsPerPage; ++i) x[i] = value_[kDestPosition + i];
        return seed::fx::MapFxPots(x);
    }

  private:
    PotFilter pots_[8];
    std::array<float, kDestCount> value_ = DefaultRegister();
    float last_[8] = {};
    bool picked_[8] = {};
    std::array<bool, kDestCount> pot_writes_{};
    Page page_ = kPagePiano;
    seed::fx::ChainMode mode_ = seed::fx::ChainMode::Run;
    bool started_ = false;
    unsigned page_changes_ = 0;
};

// MIDI CC map for external writes (any channel except 16, which carries the diagnostics):
// CC70..77 FX VR1..VR8, CC86..90 Piano attack, decay, transpose, harmonic, level. The source rates
// are edited through Mod on Daisy (app) and arrive as external writes from the main loop.
inline bool CcToWrite(uint8_t channel, uint8_t cc, uint8_t value, ExternalWrite& w) {
    if(channel == 15)
        return false;
    const float x = float(value) / 127.f;
    if(cc >= 70 && cc <= 77) {
        w = {uint8_t(kDestPosition + (cc - 70)), x};
        return true;
    }
    if(cc >= 86 && cc <= 90) {
        w = {uint8_t(kDestAttack + (cc - 86)), x};
        return true;
    }
    return false;
}

// Profiling stimulus: keeps a chosen number of voices sounding with long decays and retriggers
// the chord every 500 blocks. Pattern: 0 none, 1 one MIDI voice, 2 eight, 3 sixteen MIDI voices,
// 4 sixteen MIDI + sixteen key voices (all 32).
class PianoLoad {
  public:
    static constexpr uint8_t kPatterns = 5;
    void Select(uint8_t pattern) {
        pattern_ = pattern < kPatterns ? pattern : 0;
        blocks_ = 0;
    }
    uint8_t Pattern() const { return pattern_; }
    void Process(ToyPiano& piano) {
        static constexpr unsigned kMidi[kPatterns] = {0, 1, 8, 16, 16};
        const unsigned midi = kMidi[pattern_];
        const bool keys = pattern_ == 4;
        const bool retrigger = blocks_ % 500u == 0;
        ++blocks_;
        if(!midi && !keys)
            return;
        for(unsigned v = 0; v < midi; ++v)
            if(retrigger || !piano.MidiEnvelope(v).Active())
                piano.NoteOn(uint8_t(48 + v), 100, kAttackMin, kDecayMax);
        if(keys)
            for(unsigned k = 0; k < kNumKeys; ++k)
                if(retrigger || !piano.Envelope(k).Active())
                    piano.Trigger(k, kAttackMin, kDecayMax);
    }

  private:
    uint8_t pattern_ = 0;
    uint32_t blocks_ = 0;
};

inline unsigned ActiveVoices(const ToyPiano& piano) {
    unsigned n = 0;
    for(std::size_t k = 0; k < kNumKeys; ++k) n += piano.Envelope(k).Active() ? 1u : 0u;
    for(std::size_t m = 0; m < kNumMidiVoices; ++m) n += piano.MidiEnvelope(m).Active() ? 1u : 0u;
    return n;
}

// The three Mod on Daisy source rates (LFO 1, LFO 2, Random) live in the patch, which only the main
// loop edits. The audio thread reports each pot write as one 32-bit word: rate (14 bits), the count
// of external writes it had applied (8 bits) and a sequence number (10 bits). The main loop applies a
// pot write only if that count equals the external writes it has sent, so a pot value that predates
// an app edit is never written back over it (no stale writes).
constexpr unsigned kRateSources = 3;
constexpr uint16_t kModRateMax = 16383; // mod::kRateMax
inline uint32_t PackRateReport(uint16_t rate, uint32_t applied, uint32_t seq) {
    return uint32_t(rate & 0x3FFFu) | ((applied & 0xFFu) << 14) | ((seq & 0x3FFu) << 22);
}
inline uint16_t ReportRate(uint32_t r) { return uint16_t(r & 0x3FFFu); }
inline uint32_t ReportApplied(uint32_t r) { return (r >> 14) & 0xFFu; }
inline uint32_t ReportSeq(uint32_t r) { return r >> 22; }
inline uint16_t RateFromPosition(float x) {
    const float r = x * float(kModRateMax) + .5f;
    return uint16_t(r < 0.f ? 0.f : (r > float(kModRateMax) ? float(kModRateMax) : r));
}

// Audio thread: publishes rate pot writes.
class RateReporter {
  public:
    // Counts the external writes to the rate destinations (only those come from RateSync).
    void Count(const ExternalWrite& w) {
        if(w.dest >= kDestLfo1Rate && w.dest < kDestLfo1Rate + kRateSources) ++applied_;
    }
    template <class Store> void Publish(CumulonimbusPanel& panel, Store&& store) {
        for(unsigned i = 0; i < kRateSources; ++i)
            if(panel.TakePotWrite(Dest(kDestLfo1Rate + i)))
                store(i, PackRateReport(RateFromPosition(panel.Value(Dest(kDestLfo1Rate + i))), applied_, ++seq_[i]));
    }

  private:
    uint32_t applied_ = 0, seq_[kRateSources] = {};
};

// Main loop: keeps the patch rates and the register in step.
class RateSync {
  public:
    // `patch`: the current patch rates. A change nobody here made (app, patch load) becomes an
    // external write to the register; returns the number pushed.
    template <class Push> unsigned FromPatch(const uint16_t (&patch)[kRateSources], Push&& push) {
        unsigned pushed = 0;
        for(unsigned i = 0; i < kRateSources; ++i) {
            if(started_ && patch[i] == applied_[i])
                continue;
            applied_[i] = patch[i];
            if(started_ && push(ExternalWrite{uint8_t(kDestLfo1Rate + i), float(patch[i]) / float(kModRateMax)})) {
                ++sent_;
                ++pushed;
            }
        }
        started_ = true;
        return pushed;
    }
    // `reports`: the audio thread's last words. Returns true and fills `rates` when the patch must change.
    bool FromPots(const uint32_t (&reports)[kRateSources], uint16_t (&rates)[kRateSources]) {
        bool change = false;
        for(unsigned i = 0; i < kRateSources; ++i) {
            rates[i] = applied_[i];
            const uint32_t r = reports[i];
            if(!r || ReportSeq(r) == seen_[i])
                continue;
            seen_[i] = ReportSeq(r);
            if(ReportApplied(r) != (sent_ & 0xFFu)) {
                ++stale_;
                continue; // written before an external edit reached the audio thread
            }
            if(ReportRate(r) != applied_[i]) {
                applied_[i] = rates[i] = ReportRate(r);
                change = true;
            }
        }
        return change;
    }
    uint32_t Stale() const { return stale_; }

  private:
    uint16_t applied_[kRateSources] = {};
    uint32_t seen_[kRateSources] = {};
    uint32_t sent_ = 0, stale_ = 0;
    bool started_ = false;
};

// Mono piano sum -> chain (mono grain ring, stereo pan and reverb) -> stereo output.
class CumulonimbusHost {
  public:
    using Chain = seed::fx::GranularReverbChain;
    static constexpr int32_t kPlayableCap = 16; // conservative boot cap (MEASURED cap pending)

    bool Prepare(Chain::Grains::Storage& grains, seed::fx::TorusReverb::Storage& reverb, int32_t cap = kPlayableCap) {
        panel_.Reset();
        Chain::Grains::Options o;
        o.channels = 1;
        o.cap = cap;
        if(!chain_.Prepare(grains, reverb, o))
            return false;
        Apply();
        return true;
    }
    void ReadControls(const std::array<float, 8>& raw, bool sw17, int sw18) { panel_.Update(raw, sw17, sw18); }
    void Write(const ExternalWrite& w) { panel_.Write(w); }
    // Control rate, after the matrix. `position`, `grain_mix`, `hall_mix`: 0..1 after modulation.
    void SetModulated(float position, float grain_mix, float hall_mix) {
        seed::fx::FxControls c = panel_.Fx();
        c.grains.position = position;
        c.chain.grain_mix = grain_mix;
        c.chain.hall_mix = hall_mix;
        chain_.SetMode(panel_.Mode());
        chain_.SetGrainParameters(c.grains);
        chain_.SetSettings(c.chain);
    }
    void Apply() {
        const seed::fx::FxControls c = panel_.Fx();
        SetModulated(c.grains.position, c.chain.grain_mix, c.chain.hall_mix);
    }
    void Process(const float* mono, float* out_left, float* out_right, std::size_t frames) {
        chain_.Process(mono, mono, out_left, out_right, frames);
    }
    CumulonimbusPanel& Panel() { return panel_; }
    const CumulonimbusPanel& Panel() const { return panel_; }
    Chain& GetChain() { return chain_; }
    const Chain& GetChain() const { return chain_; }

  private:
    CumulonimbusPanel panel_;
    Chain chain_;
};

} // namespace toy_piano
