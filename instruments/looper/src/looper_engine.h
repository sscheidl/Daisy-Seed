// SPDX-License-Identifier: MIT
// Looper engine core v0.1: an independent implementation of the tape-looper
// behavior described in docs/STARDUST_ANALYSIS.md, sections 4 and 6. No
// hardware dependencies; buffer storage is injected by the caller.
#pragma once
#include <cstddef>
#include <cstdint>

namespace looper {

struct Frame {
    float left = 0.f;
    float right = 0.f;
};

enum class LoopMode : uint8_t { SoundOnSound, Replace };
// Window: the original's gain dip to zero over 384 frames at the loop seam.
// Crossfade: the seam blends into the recorded continuation instead.
enum class SeamMode : uint8_t { Window, Crossfade };
enum class State : uint8_t { Empty, Recording, Looping };

constexpr uint32_t kMaxFrames = 4176000;       // 87.0 s at 48 kHz per buffer
constexpr uint32_t kRecordFadeFrames = 960;    // 20 ms first-take fade-in
constexpr uint32_t kTailFrames = 960;          // post-roll kept for the crossfade seam
constexpr uint32_t kPunchFadeFrames = 960;     // overdub punch-in/out
constexpr uint32_t kOpRampFrames = 480;        // undo, redo, erase
constexpr uint32_t kReverseRampFrames = 480;   // direction change
constexpr uint32_t kSeamFrames = 384;          // seam window, crossfade and jump length
constexpr uint32_t kMinWindowFrames = 1152;    // smallest Start/Size window
constexpr uint32_t kCopyBlockFrames = 128;     // copy-on-write granularity
constexpr uint32_t kSyncFramesPerSample = 32;  // background undo-copy budget
constexpr float kMinSemitones = -24.f;
constexpr float kMaxSemitones = 36.f;
constexpr float kMaxSpeed = 8.f;

// Single-threaded: every call, including Process, must come from the same
// thread (the audio callback on the firmware).
class Engine {
  public:
    // Each buffer must hold capacityFrames frames (1 <= capacity <= kMaxFrames).
    // The buffers are not cleared; only written frames are ever read.
    bool Init(float sampleRate, Frame* buffer0, Frame* buffer1, uint32_t capacityFrames);
    void Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                 size_t frames);

    // Empty: start or stop the first take. Looping: start or stop an overdub.
    void PressRecord();
    void SetPlaying(bool playing);
    void TogglePlay() { SetPlaying(!playing_); }
    void Undo();
    void Redo();
    void Erase();
    void SetReverse(bool reverse) { reverseRequest_ = reverse; }
    void ToggleReverse() { reverseRequest_ = !reverseRequest_; }
    // Bounds-checked replacement of the loop, undoable like a layer. Rejects
    // 0 or more than the capacity and any call while the engine is busy; a
    // rejected call changes neither data nor state. Not real-time safe.
    bool LoadLoop(const Frame* source, uint32_t frames);

    void SetLoopMode(LoopMode mode) { mode_ = mode; }
    void SetSeamMode(SeamMode mode) { seamRequest_ = mode; }
    void SetVarispeed(float semitones);
    void SetInertia(float amount);        // 0..1, below 0.03 off, up to tau = 2 s
    void SetInertialSlope(float slope);   // 0..1, 0.5 lags both directions
    void SetStart(float start);           // 0..1 of the loop length
    void SetSize(float size);             // 0..1, window 1152 + size^2 (len - 1152)

    State GetState() const { return state_; }
    bool Overdubbing() const { return overdub_ || recGain_ > 0.f; }
    bool RecordPending() const { return recordRequest_; }
    bool Playing() const { return playing_; }
    bool Reversed() const { return direction_ < 0; }
    bool CanUndo() const { return canUndo_; }
    bool CanRedo() const { return canRedo_; }
    bool OperationPending() const { return pendingOp_ != Op::None; }
    bool UndoCopyPending() const { return syncPending_; }
    // Recording, punch fade, post-roll, pending operation or undo copy running.
    bool Busy() const;
    LoopMode GetLoopMode() const { return mode_; }
    SeamMode GetSeamMode() const { return seamRequest_; }
    uint32_t Capacity() const { return capacity_; }
    uint32_t Length() const { return length_[active_]; }
    uint32_t TailLength() const { return tail_[active_]; }
    uint32_t WindowStart() const { return windowStart_; }
    uint32_t WindowSize() const { return windowSize_; }
    uint32_t Position() const;  // absolute frame of the head
    float VarispeedTarget() const { return targetSemitones_; }
    float Inertia() const { return inertia_; }
    float Speed() const { return speed_; }
    float Increment() const { return increment_; }
    // Stored frame of the current state (index < Length() + TailLength()).
    Frame StoredFrame(uint32_t index) const;

  private:
    enum class Op : uint8_t { None, Undo, Redo, Erase };
    enum class TailMode : uint8_t { Off, Tail, Gate };
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
    void StartOverdub();
    void RequestOperation(Op op);
    bool CanStartOverdub() const;
    void ApplyWindow();
    void ComputeWindow(uint32_t& start, uint32_t& size) const;
    void ResetHead(uint32_t absolute);
    void UpdateSeamLatches();
    TailMode DesiredTailMode() const;
    void AdvanceHead(float inLeft, float inRight, float& outLeft, float& outRight);
    void UpdateCoefficients();

    uint32_t Absolute(uint32_t relative) const { return Absolute(relative, windowStart_); }
    uint32_t Absolute(uint32_t relative, uint32_t start) const;
    Frame Raw(uint32_t index) const;
    Frame Composite(uint32_t absolute) const;
    Frame ReadSeam(uint32_t relative) const { return ReadSeam(relative, windowStart_, windowSize_); }
    Frame ReadSeam(uint32_t relative, uint32_t start, uint32_t size) const;
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

    // Copy-on-write snapshot of the previous state into the other buffer.
    bool syncPending_ = false;
    uint8_t syncSource_ = 0;
    uint32_t syncBlocks_ = 0, syncCursor_ = 0, syncCredit_ = 0;
    uint32_t copied_[(kMaxBlocks + 31) / 32] = {};

    uint32_t recordPosition_ = 0;
    uint32_t postRoll_ = 0, postRollLength_ = 0;
    bool overdub_ = false, recordRequest_ = false;
    float recGain_ = 0.f;
    LoopMode mode_ = LoopMode::SoundOnSound, writeMode_ = LoopMode::SoundOnSound;
    Op pendingOp_ = Op::None;
    float opGain_ = 1.f;

    int direction_ = 1;
    bool reverseRequest_ = false;
    float directionGain_ = 1.f;

    Head head_, jump_;
    bool jumping_ = false;
    float jumpGain_ = 0.f;
    uint32_t jumpStart_ = 0, jumpSize_ = 0;  // window the jump head keeps reading
    float phase_ = 0.f;
    float previousInLeft_ = 0.f, previousInRight_ = 0.f;

    float start_ = 0.f, size_ = 1.f;
    bool windowDirty_ = false;
    uint32_t windowStart_ = 0, windowSize_ = 0;
    SeamMode seamRequest_ = SeamMode::Window, edgeMode_ = SeamMode::Window;
    TailMode tailMode_ = TailMode::Off;

    bool playing_ = true;
    float targetSemitones_ = 0.f, glideSemitones_ = 0.f, ratio_ = 1.f;
    float speed_ = 0.f, increment_ = 0.f;
    float inertia_ = 0.f, slope_ = 0.5f;
    float coefficientUp_ = 1.f, coefficientDown_ = 1.f;
};

}  // namespace looper
