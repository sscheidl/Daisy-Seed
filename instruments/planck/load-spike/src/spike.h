// MIT. Portable core of the S1 load spike (../README.md): the unchanged Beads processor and
// synth, driven through a fixed parameter sweep. No libDaisy, so the firmware and the host
// test run the same code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/utils/random.h"

namespace spike {

constexpr float kSampleRate = 48000.0f;
// Beads runs the codec at sample rate / kBlockRate (4000): 12 frames at 48 kHz (beads/beads.cc).
constexpr std::size_t kBlock = 12;
constexpr uint32_t kBlocksPerSecond = 4000;
// The buffer layout of the module: three banks of 256 KiB (beads/beads.cc Init) and a
// reverb buffer of 32768 words (64 KiB).
constexpr std::size_t kBankSize = 256 * 1024;
constexpr std::size_t kNumBanks = 3;
constexpr std::size_t kReverbWords = 32768;
// Not measured: grains fill up, and the delay case needs 1.5 s above SIZE 0.99 to switch
// (kDelayModeTransitionTime in beads/dsp/processor.cc).
constexpr uint32_t kWarmupBlocks = 2 * kBlocksPerSecond;
constexpr uint32_t kPointBlocks = kBlocksPerSecond; // 1 s per sweep point on the kit

// S1 cases of the port concept, section 9.
enum Case : uint8_t {
    kGranular,  // (a) hi-fi 16 bit, granular, reverb
    kDelay,     // (b) hi-fi 16 bit, delay mode (SIZE fully clockwise)
    kDirtyTape, // (c) µ-law at 48 kHz with 2:1 conversion, wow and flutter, reverb
    kSynth,     // (d) wavetable synth mode
    kNumCases
};

// Panel values as the module feeds them to the processor: DENSITY and SIZE 0..1, PITCH in
// semitones (law LAW_DETUNE, up to +24), TIME 0..1, attenurandomizer -1..1 (negative: a new
// random value per grain).
struct Point {
    float density, size, pitch, time, time_amount;
};

// DENSITY 0 and 1 give the highest grain rate (130.8 Hz and half of it, trigger_generator.cc);
// 0.48-0.52 is the manual dead zone and is left out. SIZE 0 and 0.97 give the longest grains
// (4 s, reverse and forward; granular_player.h ComputeGrainDuration), so 32 grains overlap.
// PITCH +24 reads the buffer four times faster. TIME randomization scatters the grains over
// the whole buffer: the case that a cache dislikes.
constexpr float kDensity[] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
constexpr float kSize[] = {0.0f, 0.5f, 0.97f};
constexpr float kPitch[] = {0.0f, 24.0f};
constexpr float kTimeAmount[] = {0.0f, -1.0f};
constexpr float kDelayTime[] = {0.2f, 0.9f};
constexpr std::size_t kGranularPoints = 6 * 3 * 2 * 2;
constexpr std::size_t kDelayPoints = 6 * 2 * 2 * 2;
constexpr std::size_t kMaxPoints = kGranularPoints;

constexpr std::size_t PointCount(Case c) { return c == kDelay ? kDelayPoints : kGranularPoints; }

inline Point GetPoint(Case c, std::size_t i) {
    Point p{};
    p.time_amount = kTimeAmount[i % 2];
    p.pitch = kPitch[(i / 2) % 2];
    if(c == kDelay) {
        p.size = 1.0f;
        p.time = kDelayTime[(i / 4) % 2];
        p.density = kDensity[(i / 8) % 6];
    } else {
        p.size = kSize[(i / 4) % 3];
        p.time = 0.5f;
        p.density = kDensity[(i / 12) % 6];
    }
    return p;
}

inline uint32_t TotalBlocks(Case c, uint32_t point_blocks) {
    return kWarmupBlocks + uint32_t(PointCount(c)) * point_blocks;
}

constexpr int kWarmup = -1;

// Modulation targets of the product-load mode (product.h). Offsets are normalized like the
// panel values; PITCH maps 1.0 to 48 semitones.
enum Target : uint8_t { kModTime, kModSize, kModShape, kModPitch, kModDensity, kModFeedback, kModDryWet, kModReverb, kNumTargets };
constexpr float kPitchSpan = 48.0f;

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Owned by one side at a time: the main loop calls Start() while the audio callback is idle,
// the audio callback calls Process() until done().
class Engine {
  public:
    void Init(uint8_t* banks[kNumBanks], uint16_t* reverb, uint32_t point_blocks = kPointBlocks) {
        for(std::size_t i = 0; i < kNumBanks; ++i)
            banks_[i] = banks[i];
        reverb_ = reverb;
        point_blocks_ = point_blocks;
        block_ = total_ = 0;
    }

    // Fresh, reproducible state for each case: cleared memory, fixed seeds. Not real-time
    // (clears 832 KiB). Returns false if the processor does not run this case at 48 kHz.
    bool Start(Case c) {
        for(std::size_t i = 0; i < kNumBanks; ++i)
            std::memset(banks_[i], 0, kBankSize);
        std::memset(reverb_, 0, kReverbWords * sizeof(uint16_t));
        stmlib::Random::Seed(0x21);
        noise_ = 0x9E3779B9u;
        case_ = c;
        block_ = 0;
        total_ = TotalBlocks(c, point_blocks_);
        if(c == kSynth) {
            synth_.Init(reverb_);
            return true;
        }
        processor_.Init(banks_, reverb_);
        processor_.Configure(c == kDirtyTape ? beads::Processor::MEDIA_DIRTY_TAPE : beads::Processor::MEDIA_DIGITAL,
                             false);
        // The kit has no 24 kHz codec: case (c) must be the 48 kHz branch with 2:1 conversion.
        return processor_.sample_rate() == kSampleRate && processor_.ratio() == (c == kDirtyTape ? 2 : 1);
    }

    // One block of kBlock frames. Returns the sweep point it belongs to, or kWarmup.
    // `offset` (kNumTargets values, or nullptr) adds modulation to the sweep values; with
    // nullptr or all zero the parameters are exactly those without modulation.
    int Process(beads::FloatFrame* out, const float* offset = nullptr) {
        const int point = block_ < kWarmupBlocks ? kWarmup : int((block_ - kWarmupBlocks) / point_blocks_);
        const Point pt = GetPoint(case_, point < 0 ? 0 : std::size_t(point));
        static const float kZero[kNumTargets] = {};
        const float* o = offset ? offset : kZero;

        beads::Parameters p;
        Set(p.time, Clamp01(pt.time + o[kModTime]), pt.time_amount);
        Set(p.size, Clamp01(pt.size + o[kModSize]), 0.0f);
        Set(p.shape, Clamp01(0.5f + o[kModShape]), 0.0f);
        Set(p.pitch, pt.pitch + o[kModPitch] * kPitchSpan, 0.0f);
        p.monophonic_mode = false;
        beads::ModulatedParameter density;
        density.base = Clamp01(pt.density + o[kModDensity]);
        density.cv = 0.0f;
        beads::Processor::MixParameters mix;
        mix.feedback = Clamp01(0.5f + o[kModFeedback]); // also selects the wavetable bank in synth mode
        mix.dry_wet = Clamp01(1.0f + o[kModDryWet]);
        mix.reverb_amount = Clamp01(1.0f + o[kModReverb]);
        mix.output_gate = false;
        bool freeze[kBlock] = {};
        // Grains need the SEED gate high: the pulse train only advances while it is
        // (trigger_generator.cc RenderPulseTrain). The granular cases latch SEED, as a player
        // does for a continuous cloud. Delay mode runs with SEED off, its default.
        const beads::SeedButtonState seed =
            case_ == kDelay ? beads::SEED_BUTTON_STATE_OFF : beads::SEED_BUTTON_STATE_LOCKED;

        if(case_ == kSynth) {
            synth_.Render(seed, nullptr, freeze, beads::Synth::RESOLUTION_16_BIT, density, p,
                          mix, out, kBlock);
        } else {
            beads::FloatFrame in[kBlock];
            for(auto& f : in) { // white noise at -12 dBFS, independent channels
                f.l = Noise();
                f.r = Noise();
            }
            processor_.Process(seed, nullptr, freeze, density, p, mix, in, out, kBlock);
        }
        ++block_;
        return point;
    }

    bool done() const { return block_ >= total_; }
    Case current() const { return case_; }
    uint32_t block() const { return block_; }
    uint32_t total() const { return total_; }
    beads::Processor& processor() { return processor_; }
    beads::Synth& synth() { return synth_; }

  private:
    static void Set(beads::RandomizedParameter& r, float base, float amount) {
        r.base = base;
        r.attenuverter = amount;
        r.cv = 0.0f;
        r.has_cv = false;
    }
    float Noise() {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return (float(noise_) / 4294967296.0f - 0.5f) * 0.5f;
    }

    beads::Processor processor_;
    beads::Synth synth_;
    uint8_t* banks_[kNumBanks] = {};
    uint16_t* reverb_ = nullptr;
    uint32_t point_blocks_ = kPointBlocks, block_ = 0, total_ = 0, noise_ = 0;
    Case case_ = kGranular;
};

// One audio callback of `size` frames (a multiple of kBlock): the engine runs once per kBlock
// frames, exactly the calls of a 12-frame callback, so the output does not depend on the
// callback size. After the last block of a case the rest is silence. Returns the sweep point of
// the last rendered block (all blocks of one callback share it: the point lengths are multiples
// of 4 blocks).
inline int RenderCallback(Engine& engine, float* left, float* right, std::size_t size, const float* offset,
                          float gain) {
    int point = kWarmup;
    for(std::size_t first = 0; first + kBlock <= size; first += kBlock) {
        beads::FloatFrame sub[kBlock];
        if(engine.done()) {
            for(auto& f : sub)
                f.l = f.r = 0.0f;
        } else {
            point = engine.Process(sub, offset);
        }
        for(std::size_t i = 0; i < kBlock; ++i) {
            left[first + i] = sub[i].l * gain;
            right[first + i] = sub[i].r * gain;
        }
    }
    return point;
}
static_assert(kWarmupBlocks % 4 == 0 && kPointBlocks % 4 == 0, "48-frame callbacks must not straddle two points");

// Cycle statistics of one case. Warm-up blocks count only towards overruns.
struct Result {
    uint32_t blocks = 0, peak = 0, overruns = 0, warmup_peak = 0;
    uint64_t sum = 0;
    uint8_t worst_point = 0;
    uint32_t point_peak[kMaxPoints] = {};

    void Add(int point, uint32_t cycles, uint32_t budget) {
        if(cycles > budget)
            ++overruns;
        if(point < 0 || std::size_t(point) >= kMaxPoints) {
            warmup_peak = cycles > warmup_peak ? cycles : warmup_peak;
            return;
        }
        ++blocks;
        sum += cycles;
        if(cycles > point_peak[point])
            point_peak[point] = cycles;
        if(cycles > peak) {
            peak = cycles;
            worst_point = uint8_t(point);
        }
    }
    uint32_t average() const { return blocks ? uint32_t(sum / blocks) : 0; }
};

// Load in 0.01 % of the block budget, as the other bench firmwares report it.
inline uint32_t Load(uint32_t cycles, uint32_t budget) {
    return budget ? uint32_t((uint64_t(cycles) * 10000u + budget / 2) / budget) : 0;
}

} // namespace spike
