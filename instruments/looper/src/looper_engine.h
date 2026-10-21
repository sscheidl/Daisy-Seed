// SPDX-License-Identifier: MIT
// Looper engine: an independent implementation of the tape-looper behavior
// described in docs/STARDUST_ANALYSIS.md, sections 4 and 6 to 8. No hardware
// dependencies; buffer storage is injected by the caller.
#pragma once
#include <cstddef>
#include <cstdint>

namespace looper {

struct Frame {
    float left = 0.f;
    float right = 0.f;
};

enum class LoopMode : uint8_t { SoundOnSound, Replace, Frippertronics, Resample };
// Window: the original's gain dip to zero over 384 frames at the loop seam.
// Crossfade: the seam blends into the recorded continuation instead.
enum class SeamMode : uint8_t { Window, Crossfade };
enum class State : uint8_t { Empty, Recording, Looping };

constexpr uint32_t kMaxFrames = 4176000;       // 87.0 s at 48 kHz per buffer
constexpr uint32_t kRecordFadeFrames = 960;    // 20 ms first-take fade-in
constexpr uint32_t kTailFrames = 960;          // post-roll kept for the crossfade seam
constexpr uint32_t kPunchFadeFrames = 960;     // overdub punch-in/out
constexpr uint32_t kOpRampFrames = 480;        // undo, redo, erase, resample commit
constexpr uint32_t kReverseRampFrames = 480;   // direction change
constexpr uint32_t kSeamFrames = 384;          // seam window, crossfade and jump length
constexpr uint32_t kMinWindowFrames = 1152;    // smallest Start/Size, slice and repeat window
constexpr uint32_t kCopyBlockFrames = 128;     // copy-on-write granularity
constexpr uint32_t kSyncFramesPerSample = 32;  // background undo-copy budget
constexpr float kMinSemitones = -24.f;
constexpr float kMaxSemitones = 36.f;
constexpr float kMaxSpeed = 8.f;
constexpr float kMaxRetention = 0.999f;                        // Frippertronics k at decay 0
constexpr float kDefaultDecay = 1.f - 0.707f / kMaxRetention;  // decay knob for k = 0.707
constexpr uint32_t kFreeSegmentLimit = 192000;  // shorter windows form one base segment
constexpr uint32_t kSegmentTarget = 96000;      // longer ones split into 1-2 s segments

// Counters of Slice/Skip decisions, for tests and diagnostics.
struct SliceEvents {
    uint32_t boundaries = 0, repeats = 0, offsets = 0, reverses = 0, pitches = 0;
};

// Single-threaded: every call, including Process, must come from the same
// thread (the audio callback on the firmware).
class Engine {
  public:
    // Each buffer must hold capacityFrames frames (1 <= capacity <= kMaxFrames).
    // The buffers are not cleared; only written frames are ever read.
    bool Init(float sampleRate, Frame* buffer0, Frame* buffer1, uint32_t capacityFrames);
    void Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                 size_t frames);

    // Empty: start or stop the first take. Looping: start or stop an overdub,
    // or in Resample mode a capture of the output into the other buffer.
    void PressRecord();
    void SetPlaying(bool playing);
    void TogglePlay() { SetPlaying(!playing_); }
    // Undo and redo are ignored while the loop mode is Frippertronics.
    void Undo();
    void Redo();
    void Erase();
    void SetReverse(bool reverse) { reverseRequest_ = reverse; }
    void ToggleReverse() { reverseRequest_ = !reverseRequest_; }
    // Bounds-checked replacement of the loop, undoable like a layer. Rejects
    // 0 or more than the capacity and any call while the engine is busy; a
    // rejected call changes neither data nor state. Not real-time safe.
    bool LoadLoop(const Frame* source, uint32_t frames);
    // Streaming import, e.g. from a file in another thread: BeginImport hands
    // out the other buffer for `frames` frames (nullptr when busy or out of
    // range). Until CommitImport or CancelImport the engine keeps playing but
    // ignores record, undo, redo and erase and never touches that buffer, so
    // the caller may fill it concurrently. CommitImport switches to the new
    // loop of `length` frames plus `tail` post-roll frames with a 10 ms fade;
    // `undoable` keeps the previous state as the undo state.
    Frame* BeginImport(uint32_t frames);
    bool CommitImport(uint32_t length, uint32_t tail, bool undoable = true);
    void CancelImport() { importing_ = false; }

    void SetLoopMode(LoopMode mode) { mode_ = mode; }
    void SetSeamMode(SeamMode mode) { seamRequest_ = mode; }
    void SetVarispeed(float semitones);
    void SetInertia(float amount);        // 0..1, below 0.03 off, up to tau = 2 s
    void SetInertialSlope(float slope);   // 0..1, 0.5 lags both directions
    void SetStart(float start);           // 0..1 of the loop length
    void SetSize(float size);             // 0..1, window 1152 + size^2 (len - 1152)
    // Frippertronics decay as in the manual: 0 keeps the old material
    // (k = 0.999 per pass), 1 erases it on every pass (k = 0).
    void SetFrippertronicsDecay(float decay);
    void SetSlice(float slice);  // 0..1: divisor 2^floor(4.9 slice) and random repeats
    void SetSkip(float skip);    // 0..1: six zones of random slice transformations
    void SetRandomSeed(uint32_t seed) { random_ = seed ? seed : 1u; }

    State GetState() const { return state_; }
    bool Overdubbing() const { return overdub_ || recGain_ > 0.f; }
    bool Resampling() const { return capture_ != Capture::Off; }
    bool RecordPending() const { return recordRequest_; }
    bool Playing() const { return playing_; }
    bool Reversed() const { return reverseRequest_; }         // as requested by the user
    bool PlayingBackwards() const { return direction_ < 0; }  // including slice reverse
    bool CanUndo() const { return canUndo_ && mode_ != LoopMode::Frippertronics; }
    bool CanRedo() const { return canRedo_ && mode_ != LoopMode::Frippertronics; }
    bool OperationPending() const { return pendingOp_ != Op::None; }
    bool UndoCopyPending() const { return syncPending_; }
    bool Importing() const { return importing_; }
    // Changes whenever the stored loop or its length may change. While the
    // engine is not Busy() the active buffer is only read, so another thread
    // may copy ActiveFrames() and check the version afterwards.
    uint32_t ContentVersion() const { return version_; }
    const Frame* ActiveFrames() const { return buffer_[active_]; }
    // Recording, punch fade, post-roll, capture, pending operation or undo copy.
    bool Busy() const;
    LoopMode GetLoopMode() const { return mode_; }
    SeamMode GetSeamMode() const { return seamRequest_; }
    uint32_t Capacity() const { return capacity_; }
    uint32_t Length() const { return length_[active_]; }
    uint32_t TailLength() const { return tail_[active_]; }
    uint32_t WindowStart() const { return windowStart_; }
    uint32_t WindowSize() const { return windowSize_; }
    uint32_t PlayStart() const { return playStart_; }  // where the head loops now:
    uint32_t PlaySize() const { return playSize_; }    // the repeat window while one runs
    uint32_t Position() const;                         // absolute frame of the head
    float VarispeedTarget() const { return targetSemitones_; }
    float Inertia() const { return inertia_; }
    float Speed() const { return speed_; }
    float Increment() const { return increment_; }
    float FrippertronicsDecay() const { return decay_; }
    float FrippertronicsRetention() const { return retention_; }
    uint32_t SliceLength() const { return sliceLength_; }
    uint32_t SliceDivisor() const { return sliceDivisor_; }
    bool Repeating() const { return repeating_; }
    bool SliceOffsetActive() const { return sliceOffset_; }
    float SlicePitch() const { return pitch_; }
    const SliceEvents& Events() const { return events_; }
    // Blocks copied for undo snapshots so far, for tests and diagnostics.
    uint32_t CopiedBlocks() const { return copiedBlocks_; }
    // Stored frame of the current state (index < Length() + TailLength()).
    Frame StoredFrame(uint32_t index) const;

  private:
    enum class Op : uint8_t { None, Undo, Redo, Erase, Commit };
    enum class TailMode : uint8_t { Off, Tail, Gate };
    enum class Capture : uint8_t { Off, Recording, PostRoll, Waiting };
    struct Head {
        uint32_t index = 0;  // relative to the head's window
        Frame previous, current;
    };

    void ProcessSample(float inLeft, float inRight, float& outLeft, float& outRight);
    void RecordFirstTake(float inLeft, float inRight);
    void FinishFirstTake();
    void WritePostRoll(float inLeft, float inRight);
    void UpdateSpeed();
    void UpdateRamps();
    void ExecuteOperation();
    void StartFirstTake();
    void StartLayer();
    void StartOverdub();
    void StartCapture();
    void StopCapture();
    void CaptureSample(float left, float right);
    void RequestOperation(Op op);
    bool CanStartLayer() const;
    void EnterLoop(uint32_t absolute);
    void ApplyWindow();
    void ComputeWindow(uint32_t& start, uint32_t& size) const;
    void PlayWindowTarget(uint32_t& start, uint32_t& size) const;
    bool MoveHead(uint32_t start, uint32_t size, uint32_t absolute);
    void StartJump();
    void ResetHead(uint32_t absolute);
    void UpdateSeamLatches();
    TailMode DesiredTailMode() const;
    void AdvanceHead(float inLeft, float inRight, float& outLeft, float& outRight);
    void UpdateCoefficients();

    void UpdateSegmentation();
    void ResetSlices();
    void AdvanceSlices();
    void SliceBoundary();
    void StartPitch(float target, uint32_t glideFrames);
    uint32_t GlideFrames(float fraction);
    uint32_t NextRandom();
    float Uniform() { return float(NextRandom() >> 8) * (1.f / 16777216.f); }
    bool Chance(float p) { return Uniform() < p; }
    uint32_t Below(uint32_t n) { return n > 1 ? NextRandom() % n : 0; }

    uint32_t Absolute(uint32_t relative) const { return Absolute(relative, playStart_); }
    uint32_t Absolute(uint32_t relative, uint32_t start) const;
    uint32_t RelativeTo(uint32_t absolute, uint32_t start) const;
    Frame Raw(uint32_t index) const;
    Frame Composite(uint32_t absolute, TailMode tail) const;
    Frame ReadSeam(uint32_t relative) const {
        return ReadSeam(relative, playStart_, playSize_, tailMode_);
    }
    Frame ReadSeam(uint32_t relative, uint32_t start, uint32_t size, TailMode tail) const;
    void WriteFrame(uint32_t absolute, Frame input, float gain);
    void EnsureCopied(uint32_t index);
    void CopyBlock(uint32_t block);
    bool Copied(uint32_t block) const { return copied_[block >> 5] & (1u << (block & 31)); }
    void SyncStep(size_t frames);

    static constexpr uint32_t kMaxBlocks = (kMaxFrames + kCopyBlockFrames - 1) / kCopyBlockFrames;

    bool ready_ = false;
    float sampleRate_ = 48000.f;
    Frame* buffer_[2] = {nullptr, nullptr};
    uint32_t capacity_ = 0;
    uint32_t length_[2] = {0, 0};
    uint32_t tail_[2] = {0, 0};
    uint8_t active_ = 0;
    State state_ = State::Empty;
    bool canUndo_ = false, canRedo_ = false;
    bool mirror_ = false;  // both buffers hold identical data for the current state
    uint32_t version_ = 0;
    bool importing_ = false;
    uint32_t importFrames_ = 0;

    // Copy-on-write snapshot of the previous state into the other buffer.
    bool syncPending_ = false;
    uint8_t syncSource_ = 0;
    uint32_t syncBlocks_ = 0, syncCursor_ = 0, syncCredit_ = 0;
    uint32_t writeCopies_ = 0;  // frames copied on write since the last SyncStep
    uint32_t copiedBlocks_ = 0;
    uint32_t copied_[(kMaxBlocks + 31) / 32] = {};

    uint32_t recordPosition_ = 0;
    uint32_t postRoll_ = 0, postRollLength_ = 0;
    bool overdub_ = false, recordRequest_ = false;
    float recGain_ = 0.f;
    LoopMode mode_ = LoopMode::SoundOnSound, writeMode_ = LoopMode::SoundOnSound;
    Op pendingOp_ = Op::None;
    float opGain_ = 1.f;

    // Resample: the output is captured into the other buffer, then committed.
    Capture capture_ = Capture::Off;
    uint8_t captureTarget_ = 1;
    uint32_t capturePosition_ = 0, captureLength_ = 0, capturePost_ = 0, capturePostLength_ = 0;

    float decay_ = kDefaultDecay;
    float retentionTarget_ = kMaxRetention * (1.f - kDefaultDecay);
    float retention_ = kMaxRetention * (1.f - kDefaultDecay);

    int direction_ = 1;
    bool reverseRequest_ = false;
    float directionGain_ = 1.f;

    Head head_, jump_;
    bool jumping_ = false;
    float jumpGain_ = 0.f;
    uint32_t jumpStart_ = 0, jumpSize_ = 0;  // window the jump head keeps reading
    TailMode jumpTail_ = TailMode::Off;      // and its seam
    float phase_ = 0.f;
    float previousInLeft_ = 0.f, previousInRight_ = 0.f;

    float start_ = 0.f, size_ = 1.f;
    bool windowDirty_ = false;
    uint32_t windowStart_ = 0, windowSize_ = 0;  // Start/Size window
    uint32_t playStart_ = 0, playSize_ = 0;      // the head's window
    SeamMode seamRequest_ = SeamMode::Window, edgeMode_ = SeamMode::Window;
    TailMode tailMode_ = TailMode::Off;

    // Slice/Skip
    float slice_ = 0.f, skip_ = 0.f;
    bool segmentDirty_ = true;
    uint32_t sliceLength_ = 0, sliceDivisor_ = 1, sliceCount_ = 1, repeatDepth_ = 0;
    double sliceCounter_ = 0.0;
    bool repeating_ = false, sliceReverse_ = false, sliceOffset_ = false;
    uint32_t repeatStart_ = 0, repeatSize_ = 0;  // relative to the Start/Size window
    float pitch_ = 1.f, pitchTarget_ = 1.f, pitchStep_ = 0.f;
    uint32_t glideLeft_ = 0;
    uint32_t random_ = 0x6d2b79f5u;
    SliceEvents events_;

    bool playing_ = true;
    float targetSemitones_ = 0.f, glideSemitones_ = 0.f, ratio_ = 1.f;
    float speed_ = 0.f, increment_ = 0.f;
    float inertia_ = 0.f, slope_ = 0.5f;
    float coefficientUp_ = 1.f, coefficientDown_ = 1.f;
};

}  // namespace looper
