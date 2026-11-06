#pragma once
// SPDX-License-Identifier: MIT
// The Grain Cloud engine core: voices, scheduler and renderer around the shared modules.
// Host-testable, no libDaisy. One block, all in the audio context (rule R1):
//
//   sample hand-over -> events (every block) -> control tick (every tick_blocks blocks)
//   -> births -> render: grains -> voice bus -> voice gain ramp -> float sum
//
// RenderSum() ends at the voice sum: this is the one stereo point for an effect chain (R2).
// The product runs its FX contract stages there and calls Output() last, the one clip point
// (R5). Process() is the same path with nothing in between. After Prepare() nothing allocates
// or blocks. Limits and rules: docs/BEHAVIOUR.md.
#include "control_bridge.h"
#include "envelope.h"
#include "grain_pool.h"
#include "grain_renderer.h"
#include "laws.h"
#include "modulation.h"
#include "output_stage.h"
#include "params.h"
#include "random.h"
#include "sample_view.h"
#include "step_seq/spsc.h"
#include "window_table.h"
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace grain_cloud {

struct Voice {
    bool active = false;
    bool tail = false; // stolen: spawns nothing and is no longer a note
    bool held = false, sustained = false, control_now = false;
    uint8_t note = 0;
    StartMode mode = StartMode::Grow;
    uint16_t generation = 0;
    uint16_t head = kNoGrain, count = 0;
    uint16_t cloud_left = 0;   // bounded rest of a start cloud, no event backlog
    uint32_t order = 0;        // note-on order, for stealing
    uint64_t birth_phase = 0;  // Q32: one grain per 1.0, keeps the fraction between births
    float velocity = 1.f, count_smooth = 0.f, ratio = 1.f;
    float scan = 0.f;          // offset of the position in sample spans, 0..1
    Envelope env;
    Ramp gain;
    GrainRng rng;
};

// Counters of the audio side, read outside the callback through ReadStats().
struct Stats {
    uint32_t ticks = 0, modulation_writes = 0;
    uint32_t births = 0, reverse_births = 0;
    uint32_t dropped = 0;                   // spawns refused by limit or per-block bound
    uint32_t steals = 0;                    // grains told to end sooner (B11)
    uint32_t voices_stolen = 0, deferred_note_ons = 0;
    uint32_t silent_frees = 0;              // grains freed at voice gain zero
    uint32_t hard_kills = 0;                // grains freed while audible: must stay 0
    uint32_t invalid_blocks = 0;
    uint32_t peak_active = 0, peak_births_in_block = 0, window_points_peak = 0;
    uint32_t active = 0, free = 0;
};

class Engine {
  public:
    // Startup only. false: invalid configuration.
    bool Prepare(const Config& config) {
        if(!(config.sample_rate >= 8000.f) || config.block_frames == 0 || config.block_frames > kMaxBlock ||
           config.polyphony == 0 || config.polyphony > kMaxVoices || config.pool_size == 0 ||
           config.pool_size > kMaxGrains || config.tick_blocks == 0)
            return false;
        config_ = config;
        tick_frames_ = uint32_t(config.block_frames) * config.tick_blocks;
        tick_seconds_ = float(tick_frames_) / config.sample_rate;
        count_coeff_ = 1.f - seed::dsp::Exp2(-1.44269504f * tick_seconds_ / kCountSmoothingSeconds);
        blocks_to_tick_ = 1;
        pool_.Prepare(config.pool_size);
        window_.Prepare(tick_seconds_);
        output_.Prepare(config.sample_rate);
        for(std::size_t i = 0; i < kPanPoints; ++i) {
            const float angle = float(i) / float(kPanPoints - 1) * 1.57079632679f;
            pan_left_[i] = std::cos(angle);
            pan_right_[i] = std::sin(angle);
        }
        for(std::size_t i = 0; i < kVoiceSlots; ++i) {
            voices_[i] = Voice{};
            voices_[i].rng.Seed(config.seed + uint32_t(i + 1) * 0x9E3779B1u);
        }
        for(std::size_t i = 0; i < kParamCount; ++i)
            base_[i] = kParamInfo[i].initial;
        stats_ = Stats{};
        samples_[0] = samples_[1] = SampleView{};
        sample_refs_[0] = sample_refs_[1] = 0;
        current_sample_ = 0;
        retiring_id_ = 0;
        rate_ratio_ = 1.f;
        staged_ready_.store(false, std::memory_order_relaxed);
        retired_id_.store(0, std::memory_order_relaxed);
        bend_ = 0.f;
        sustain_down_ = false;
        next_order_ = 0;
        rotate_ = 0;
        hungry_ = -1;
        rates_dirty_ = true;
        modulation_.Tick(base_, 0, live_); // no matrix attached yet: the base values
        Derive();
        prepared_ = true;
        return true;
    }
    // Startup only, after Prepare().
    void AttachModulation(mod::Engine* engine) { modulation_.Attach(engine); }

    // ---- main loop ----
    bool Post(const Event& event) { return bridge_.Post(event); }
    // Hands a sample over at the next free block boundary. false: the last one is still waiting.
    bool PublishSample(const SampleView& view) {
        if(!view.Valid() || staged_ready_.load(std::memory_order_acquire))
            return false;
        staged_ = view;
        staged_ready_.store(true, std::memory_order_release);
        return true;
    }
    // True once no grain reads the replaced sample: its memory may be reused.
    bool SampleRetired(uint32_t id) const { return retired_id_.load(std::memory_order_acquire) == id; }
    // Latest counters of the last tick. false: nothing new.
    bool ReadStats(Stats& out) {
        if(!published_.Update())
            return false;
        out = published_.Front();
        return true;
    }

    // ---- audio callback ----
    // Fills the block with the voice sum, unclipped. Effects go between this and Output().
    void RenderSum(float* left, float* right, std::size_t frames) {
        if(!prepared_ || frames != config_.block_frames) {
            for(std::size_t i = 0; i < frames; ++i)
                left[i] = right[i] = 0.f;
            ++stats_.invalid_blocks;
            return;
        }
        AdoptSample();
        DrainEvents();
        if(--blocks_to_tick_ == 0) {
            blocks_to_tick_ = config_.tick_blocks;
            ControlTick();
        } else {
            for(Voice& v : voices_)
                if(v.active && v.control_now)
                    VoiceControl(v); // a note never waits for the tick
        }
        SpawnBlock(uint32_t(frames));
        RenderVoices(left, right, frames);
    }
    OutputStage& Output() { return output_; }
    void Process(float* left, float* right, std::size_t frames) {
        RenderSum(left, right, frames);
        output_.Process(left, right, left, right, frames);
    }

    // ---- inspection, audio side or between blocks ----
    const Voice& VoiceAt(std::size_t slot) const { return voices_[slot]; }
    const GrainPool& Pool() const { return pool_; }
    const WindowTable& Window() const { return window_; }
    const Modulated& Live() const { return live_; }
    const Stats& Diagnostics() const { return stats_; }
    float Base(Param param) const { return base_[std::size_t(param)]; }
    uint32_t TickFrames() const { return tick_frames_; }
    uint32_t QueueOverflows() const { return bridge_.Overflows(); }
    std::size_t FairShare() const {
        std::size_t playing = 0;
        for(const Voice& v : voices_)
            playing += v.active && !v.tail;
        const std::size_t share = limit_ / (playing ? playing : 1);
        return share < kPreferredSlots ? kPreferredSlots : share;
    }

  private:
    static constexpr std::size_t kPanPoints = 65;
    static constexpr uint64_t kOne = uint64_t{1} << 32;

    struct Derived {
        float position = 0.f, spray = 0.f, reverse = 0.f, pan_spread = 0.f;
        float size_frames = 1.f, rate_hz = 1.f, inverse_rate = 0.f, pitch = 0.f, level = 0.f, scan_step = 0.f;
        uint32_t win_step = 1, rate_q32 = 0;
    };

    float Value(Param param) const { return base_[std::size_t(param)]; }

    // ---- control ----
    void ControlTick() {
        modulation_.Tick(base_, tick_frames_, live_); // R8: the only write of modulated values
        ++stats_.modulation_writes;
        ++stats_.ticks;
        Derive();
        window_.SetTarget(live_[Param::Window]);
        const uint32_t points = uint32_t(window_.Tick());
        if(points > stats_.window_points_peak)
            stats_.window_points_peak = points;
        if(rates_dirty_) {
            rates_ = MakeRates(Value(Param::Attack), Value(Param::Decay), Value(Param::Sustain),
                               Value(Param::Release), tick_seconds_);
            rates_dirty_ = false;
        }
        for(Voice& v : voices_) {
            if(!v.active)
                continue;
            v.scan += derived_.scan_step;
            v.scan = v.scan >= 1.f ? v.scan - 1.f : (v.scan < 0.f ? v.scan + 1.f : v.scan);
            if((v.env.Idle() && v.gain.Done() && v.gain.Value() == 0.f) || (v.tail && v.count == 0)) {
                FreeVoice(v);
                continue;
            }
            VoiceControl(v);
        }
        stats_.active = uint32_t(pool_.Active());
        stats_.free = uint32_t(pool_.Free());
        published_.Back() = stats_;
        published_.Publish();
    }

    // Period, window step and pitch are computed here, never per sample (R3).
    void Derive() {
        Derived& d = derived_;
        d.position = live_[Param::Position];
        d.spray = live_[Param::Spray];
        d.reverse = live_[Param::Direction];
        d.pan_spread = Value(Param::PanSpread);
        d.level = live_[Param::Level];
        d.pitch = PitchSemitones(live_[Param::Pitch]);
        d.size_frames = SizeSeconds(live_[Param::Size]) * config_.sample_rate;
        d.win_step = uint32_t(4294967296.f / d.size_frames);
        d.rate_hz = DensityHz(live_[Param::Density]);
        const float per_frame = d.rate_hz / config_.sample_rate;
        d.rate_q32 = uint32_t(per_frame * 4294967296.f);
        d.inverse_rate = 1.f / float(d.rate_q32);
        d.scan_step = (Value(Param::Scan) - 0.5f) * 2.f * kScanMaxPerSecond * tick_seconds_;
        const std::size_t limit = std::size_t(Value(Param::ActiveLimit) * float(pool_.Capacity()) + 0.5f);
        limit_ = limit < 1 ? 1 : limit;
    }

    // Everything the voice needs from one tick. The gain only ever moves by its ramp (R4).
    void VoiceControl(Voice& v) {
        v.control_now = false;
        v.ratio = PitchRatio(float(v.note) - 60.f + derived_.pitch + bend_);
        v.count_smooth += count_coeff_ * (float(v.count) - v.count_smooth);
        // A cloud that spreads over blocks enters mid-window, so the attack waits until it is all
        // there and shapes it whole (R7). The wait is cloud size / kMaxBirthsPerBlock blocks.
        const float level = v.cloud_left ? v.env.Level() : v.env.Step(rates_);
        if(v.env.CurrentStage() == Envelope::Stage::Hold)
            return; // frozen: the old generation runs out at the level it had
        v.gain.Set(level * v.velocity * derived_.level * CountGain(v.count_smooth) * kVoiceHeadroom, tick_frames_);
    }

    // ---- samples ----
    void AdoptSample() {
        const uint8_t other = current_sample_ ^ 1;
        if(retiring_id_ && sample_refs_[other] == 0) {
            retired_id_.store(retiring_id_, std::memory_order_release);
            samples_[other] = SampleView{};
            retiring_id_ = 0;
        }
        if(retiring_id_ || !staged_ready_.load(std::memory_order_acquire))
            return;
        retiring_id_ = samples_[current_sample_].id;
        samples_[other] = staged_;
        staged_ready_.store(false, std::memory_order_release);
        current_sample_ = other;
        rate_ratio_ = samples_[other].sample_rate / config_.sample_rate;
    }

    // ---- events ----
    void DrainEvents() {
        if(bridge_.TakeAllOff())
            ReleaseAll();
        for(uint32_t n = 0; n < kMaxEventsPerBlock; ++n) {
            const Event* event = bridge_.Peek();
            if(!event)
                return;
            if(!Apply(*event)) {
                ++stats_.deferred_note_ons;
                return; // no slot yet: a fast release frees one within kFastReleaseSeconds
            }
            bridge_.Drop();
        }
    }

    bool Apply(const Event& e) {
        switch(e.type) {
            case EventType::NoteOn:
                if(e.velocity == 0) {
                    NoteRelease(e.note);
                    return true;
                }
                return NoteStart(e.note, e.velocity);
            case EventType::NoteOff: NoteRelease(e.note); break;
            case EventType::Sustain:
                sustain_down_ = e.velocity >= 64;
                if(!sustain_down_)
                    for(Voice& v : voices_)
                        if(v.active && !v.tail && v.sustained) {
                            v.sustained = false;
                            v.env.NoteOff();
                        }
                break;
            case EventType::PitchBend:
                if(e.value == e.value)
                    bend_ = e.value;
                break;
            case EventType::SetParam:
                if(e.value == e.value) {
                    base_[std::size_t(e.param)] = Clamp01(e.value);
                    rates_dirty_ = rates_dirty_ || kParamInfo[std::size_t(e.param)].timing == Timing::Note;
                }
                break;
            case EventType::AllNotesOff: ReleaseAll(); break;
            case EventType::AllSoundOff:
                for(Voice& v : voices_)
                    if(v.active) {
                        v.tail = true;
                        v.held = v.sustained = false;
                        v.cloud_left = 0;
                        hungry_ = -1;
                        v.env.BeginFastRelease(rates_); // also ends a hold
                    }
                break;
        }
        return true;
    }

    void ReleaseAll() {
        for(Voice& v : voices_)
            if(v.active && !v.tail) {
                v.held = v.sustained = false;
                v.cloud_left = 0;
                v.env.NoteOff();
            }
    }

    void NoteRelease(uint8_t note) {
        for(Voice& v : voices_)
            if(v.active && !v.tail && v.held && v.note == note) {
                v.held = false;
                v.cloud_left = 0; // the rest of a start cloud lapses with the note
                if(sustain_down_)
                    v.sustained = true;
                else
                    v.env.NoteOff();
            }
    }

    // Old grains keep their own slot, gain and generation, so a new note can never take them
    // under its envelope. Only Keep lets them run out; every other note fades fast (C7).
    void BeginTail(Voice& v) {
        v.tail = true;
        v.held = v.sustained = false;
        v.cloud_left = 0;
        if(hungry_ == int(&v - voices_))
            hungry_ = -1;
        if(v.mode == StartMode::Keep)
            v.env.BeginHold();
        else
            v.env.BeginFastRelease(rates_);
        ++stats_.voices_stolen;
    }

    bool NoteStart(uint8_t note, uint8_t velocity) {
        const StartMode mode = StartModeFrom(Value(Param::Start));
        Voice* same = nullptr;
        std::size_t playing = 0;
        for(Voice& v : voices_)
            if(v.active && !v.tail) {
                ++playing;
                if(v.note == note)
                    same = &v;
            }
        if(same && mode == StartMode::Keep) {
            // R7 Keep: same voice, same generation, running grains go on under the new envelope.
            same->mode = mode;
            same->held = true;
            same->sustained = false;
            same->velocity = VelocityGain(velocity);
            same->order = ++next_order_;
            same->env.NoteOn();
            same->control_now = true;
            return true;
        }
        Voice* slot = nullptr;
        for(Voice& v : voices_)
            if(!v.active) {
                slot = &v;
                break;
            }
        if(!slot) {
            // Every slot is taken by a note or a tail. Shorten the oldest tail and try again.
            Voice* oldest = nullptr;
            for(Voice& v : voices_)
                if(v.tail && v.env.CurrentStage() == Envelope::Stage::Hold &&
                   (!oldest || int32_t(v.order - oldest->order) < 0))
                    oldest = &v;
            if(oldest)
                oldest->env.BeginFastRelease(rates_);
            if(same)
                BeginTail(*same);
            return false;
        }
        if(same) {
            BeginTail(*same); // a retrigger outside Keep: a new generation in a new slot
            --playing;
        }
        if(playing >= config_.polyphony)
            BeginTail(ChooseVictim());
        StartVoice(*slot, note, velocity, mode);
        return true;
    }

    // Released notes first, then the oldest.
    Voice& ChooseVictim() {
        Voice* best = nullptr;
        for(Voice& v : voices_) {
            if(!v.active || v.tail)
                continue;
            if(!best) {
                best = &v;
                continue;
            }
            const bool released = !v.held && !v.sustained, best_released = !best->held && !best->sustained;
            if((released && !best_released) ||
               (released == best_released && int32_t(v.order - best->order) < 0))
                best = &v;
        }
        return *best;
    }

    static float VelocityGain(uint8_t velocity) { return 0.25f + 0.75f * float(velocity) * (1.f / 127.f); }

    void StartVoice(Voice& v, uint8_t note, uint8_t velocity, StartMode mode) {
        v.active = true;
        v.tail = false;
        v.held = true;
        v.sustained = false;
        v.note = note;
        v.mode = mode;
        v.generation = uint16_t(v.generation + 1);
        v.order = ++next_order_;
        v.velocity = VelocityGain(velocity);
        v.head = kNoGrain;
        v.count = 0;
        v.scan = 0.f;
        v.birth_phase = kOne; // the first grain starts in this block
        v.cloud_left = 0;
        v.count_smooth = 0.f;
        v.env.Reset();
        v.gain.Reset(0.f);
        v.env.NoteOn();
        v.control_now = true;
        if(mode == StartMode::FullCloud || mode == StartMode::FullCloudAtStart) {
            // Density x duration, bounded by what the voice may hold (R7).
            const float wanted = derived_.rate_hz * derived_.size_frames / config_.sample_rate + 0.5f;
            const std::size_t share = FairShare();
            const std::size_t cloud = wanted >= float(share) ? share : std::size_t(wanted);
            v.cloud_left = uint16_t(cloud);
            v.count_smooth = float(cloud); // the law must not see a full cloud as one grain
        }
    }

    void FreeVoice(Voice& v) {
        for(uint16_t index = v.head; index != kNoGrain;) {
            Grain& g = pool_.At(index);
            const uint16_t next = g.next;
            if(v.gain.Value() == 0.f)
                ++stats_.silent_frees;
            else
                ++stats_.hard_kills;
            ReturnGrain(g, index);
            index = next;
        }
        v.head = kNoGrain;
        v.count = 0;
        v.active = v.tail = v.held = v.sustained = false;
        if(hungry_ == int(&v - voices_))
            hungry_ = -1;
    }

    void ReturnGrain(Grain& g, uint16_t index) {
        --sample_refs_[(g.flags & kGrainSampleSlot) ? 1 : 0];
        pool_.Release(index);
    }

    // ---- births ----
    void SpawnBlock(uint32_t frames) {
        steals_left_ = kMaxStealsPerBlock;
        uint32_t births = 0;
        for(std::size_t k = 0; k < kVoiceSlots; ++k) {
            Voice& v = voices_[(k + rotate_) % kVoiceSlots];
            if(!v.active || v.tail || v.env.Idle())
                continue;
            while(v.cloud_left && births < kMaxBirthsPerBlock) { // the rest waits for the next block
                --v.cloud_left;
                ++births;
                Spawn(v, 0, true);
            }
            v.birth_phase += uint64_t(derived_.rate_q32) * frames;
            while(v.birth_phase >= kOne) {
                v.birth_phase -= kOne;
                if(births >= kMaxBirthsPerBlock) {
                    ++stats_.dropped; // no backlog: this birth is gone
                    continue;
                }
                ++births;
                // How far back in the block the phase crossed one.
                const float back = float(v.birth_phase) * derived_.inverse_rate;
                const uint32_t offset = back >= float(frames) ? 0u : frames - 1u - uint32_t(back);
                Spawn(v, offset, false);
            }
        }
        rotate_ = (rotate_ + 1) % kVoiceSlots;
        if(births > stats_.peak_births_in_block)
            stats_.peak_births_in_block = births;
    }

    void Spawn(Voice& v, uint32_t offset, bool cloud) {
        const SampleView& s = samples_[current_sample_];
        if(!s.Valid())
            return;
        const int self = int(&v - voices_);
        if(pool_.Active() >= limit_) {
            ++stats_.dropped;
            if(v.count < FairShare()) {
                hungry_ = self;
                Steal(v);
            }
            return;
        }
        // Capacity freed for a voice below its share must not go back to one above it.
        if(hungry_ >= 0 && hungry_ != self && v.count >= FairShare()) {
            ++stats_.dropped;
            return;
        }
        if(hungry_ == self)
            hungry_ = -1;
        const uint16_t index = pool_.Acquire();
        if(index == kNoGrain) {
            ++stats_.dropped;
            return;
        }
        Grain& g = pool_.At(index);
        const Derived& d = derived_;

        // Every parameter of the birth column in docs/BEHAVIOUR.md is read here and only here.
        const double span = double(s.RangeFrames());
        double frames = (double(d.position) + double(v.scan)) * span;
        frames += double(v.rng.Triangular() * d.spray) * span; // B10
        const bool reverse = v.rng.Unit() < d.reverse;         // B3
        const double step = double(v.ratio) * double(rate_ratio_) * (reverse ? -1.0 : 1.0);

        g.win_step = d.win_step;
        g.win_phase = 0;
        if(cloud) { // R7: a random age; the window starts part way
            const float age = v.rng.Unit();
            g.win_phase = uint32_t(age * 4294967296.f);
            if(v.mode == StartMode::FullCloud)
                frames += double(age * d.size_frames) * step;
        }
        const bool wraps = s.Loops() && (step < 0.0 ? -step : step) < span;
        if(wraps) {
            const int64_t turns = int64_t(frames / span);
            frames -= double(turns) * span;
            if(frames < 0.0)
                frames += span;
            frames += double(s.loop_start);
        } else {
            frames += double(s.RangeStart());
            const double last = double(s.frames - 1u);
            frames = frames < 0.0 ? 0.0 : (frames > last ? last : frames);
        }
        g.pos = int64_t(frames * 4294967296.0);
        g.inc = int64_t(step * 4294967296.0);

        const float pan = (v.rng.Bipolar() * d.pan_spread + 1.f) * 0.5f * float(kPanPoints - 1);
        const std::size_t pan_index = std::size_t(pan + 0.5f);
        constexpr float kPcmScale = 1.f / 32768.f;
        g.gain_l = pan_left_[pan_index] * kPcmScale;
        g.gain_r = pan_right_[pan_index] * kPcmScale;

        g.delay = uint16_t(offset);
        g.generation = v.generation;
        g.voice = uint8_t(&v - voices_);
        g.flags = uint8_t((wraps ? kGrainWraps : 0) | (current_sample_ ? kGrainSampleSlot : 0));
        g.next = v.head;
        v.head = index;
        ++v.count;
        ++sample_refs_[current_sample_];
        ++stats_.births;
        stats_.reverse_births += reverse;
        if(pool_.Active() > stats_.peak_active)
            stats_.peak_active = uint32_t(pool_.Active());
    }

    // B11: never a hard kill. The chosen grain keeps playing, its window just runs out faster,
    // and its slot stays taken until then, so the pool limit holds. The window ends at zero
    // for every morph position. This costs no per-grain state and nothing in the render loop.
    void Steal(const Voice& requester) {
        if(steals_left_ == 0)
            return;
        const std::size_t share = FairShare();
        Voice* victim = nullptr;
        long best = 0;
        for(Voice& v : voices_) {
            if(!v.active || &v == &requester || v.env.CurrentStage() == Envelope::Stage::FastRelease)
                continue;
            long excess = long(v.count) - long(share);
            if(!v.tail && !v.held && !v.sustained && v.count)
                excess += long(pool_.Capacity()); // releasing notes give way first
            if(excess > best) {
                best = excess;
                victim = &v;
            }
        }
        if(!victim)
            return;
        // Closest to its end, so the least of the grain is lost.
        Grain* chosen = nullptr;
        uint32_t chosen_left = 0xFFFFFFFFu;
        for(uint16_t index = victim->head; index != kNoGrain; index = pool_.At(index).next) {
            Grain& g = pool_.At(index);
            const uint32_t left = 0xFFFFFFFFu - g.win_phase;
            if(uint64_t(g.win_step) * kStealFrames >= left)
                continue; // already ending that soon
            if(left < chosen_left) {
                chosen_left = left;
                chosen = &g;
            }
        }
        if(!chosen)
            return;
        chosen->win_step = chosen_left / kStealFrames + 1u;
        --steals_left_;
        ++stats_.steals;
    }

    // ---- render ----
    void RenderVoices(float* left, float* right, std::size_t frames) {
        for(std::size_t i = 0; i < frames; ++i)
            left[i] = right[i] = 0.f;
        const float* window = window_.Data();
        for(Voice& v : voices_) {
            if(!v.active)
                continue;
            float bus_left[kMaxBlock] = {}, bus_right[kMaxBlock] = {};
            uint16_t* link = &v.head;
            while(*link != kNoGrain) {
                const uint16_t index = *link;
                Grain& g = pool_.At(index);
                if(RenderGrain(g, samples_[(g.flags & kGrainSampleSlot) ? 1 : 0], window, bus_left, bus_right, frames)) {
                    link = &g.next;
                } else {
                    *link = g.next;
                    --v.count;
                    ReturnGrain(g, index);
                }
            }
            for(std::size_t i = 0; i < frames; ++i) {
                const float gain = v.gain.Next();
                left[i] += bus_left[i] * gain;
                right[i] += bus_right[i] * gain;
            }
        }
    }

    Config config_;
    bool prepared_ = false;
    uint32_t tick_frames_ = 48, blocks_to_tick_ = 1;
    float tick_seconds_ = 0.001f, count_coeff_ = 0.f;

    float base_[kParamCount]{}; // written by events, read by the control tick
    Modulated live_;            // written only by Modulation::Tick
    Derived derived_;
    EnvelopeRates rates_;
    bool rates_dirty_ = true;
    std::size_t limit_ = kMaxGrains;
    float bend_ = 0.f;
    bool sustain_down_ = false;
    uint32_t next_order_ = 0, steals_left_ = 0;
    std::size_t rotate_ = 0;
    int hungry_ = -1; // slot of a voice below its fair share that was refused a birth

    Voice voices_[kVoiceSlots];
    GrainPool pool_;
    WindowTable window_;
    OutputStage output_;
    Modulation modulation_;
    ControlBridge bridge_;
    float pan_left_[kPanPoints]{}, pan_right_[kPanPoints]{};

    SampleView samples_[2];
    int32_t sample_refs_[2]{};
    uint8_t current_sample_ = 0;
    uint32_t retiring_id_ = 0;
    float rate_ratio_ = 1.f;
    SampleView staged_;
    std::atomic<bool> staged_ready_{false};
    std::atomic<uint32_t> retired_id_{0};

    Stats stats_;
    step_seq::TripleBuffer<Stats> published_;
};

} // namespace grain_cloud
