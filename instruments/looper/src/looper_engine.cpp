// SPDX-License-Identifier: MIT
#include "looper_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace looper {
namespace {

constexpr float kGlideCoefficient = 0.001f;    // one-pole per sample, about 21 ms
constexpr float kGlideJumpSemitones = 4.f;     // larger target changes are immediate
constexpr float kIncrementCoefficient = 0.1f;  // head increment smoothing per sample
constexpr float kRetentionCoefficient = 0.005f;  // Frippertronics k smoothing, about 4 ms
constexpr float kInertiaOff = 0.03f;
constexpr float kSlopeDeadBand = 0.05f;
constexpr float kKnobDeadBand = 0.003f;
constexpr float kSliceActive = 0.02f;
constexpr float kSkipActive = 0.01f;
constexpr float kRepeatChance = 0.6f;
constexpr float kOffsetChance = 0.5f;
constexpr float kPitchChance = 0.25f;

Frame Mix(Frame a, Frame b, float t) {
    return {a.left + (b.left - a.left) * t, a.right + (b.right - a.right) * t};
}
Frame Scale(Frame a, float g) { return {a.left * g, a.right * g}; }
float Clamp(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }
float KnobDeadBand(float x) {
    if (!(x >= kKnobDeadBand))  // also maps NaN to 0
        return 0.f;
    return x > 1.f - kKnobDeadBand ? 1.f : x;
}
float Knob(float x) { return x == x ? Clamp(x, 0.f, 1.f) : 0.f; }
// Transparent below 0.95, soft knee towards 1.0 above.
float SoftLimit(float x) {
    const float a = std::fabs(x);
    if (a <= .95f)
        return x;
    const float e = a - .95f;
    return std::copysign(.95f + .05f * e / (.05f + e), x);
}
Frame SoftLimit(Frame f) { return {SoftLimit(f.left), SoftLimit(f.right)}; }
// Linear gain over the first and last `ramp` frames of a span; exactly 0 at both ends.
float EdgeGain(uint32_t index, uint32_t span, uint32_t ramp = kSeamFrames) {
    float g = 1.f;
    if (index < ramp)
        g = float(index) / float(ramp);
    const uint32_t back = span - 1 - index;
    if (back < ramp)
        g = std::min(g, float(back) / float(ramp));
    return g;
}

}  // namespace

bool Engine::Init(float sampleRate, Frame* buffer0, Frame* buffer1, uint32_t capacityFrames) {
    *this = Engine();
    if (!(sampleRate > 0.f) || buffer0 == nullptr || buffer1 == nullptr || capacityFrames == 0 ||
        capacityFrames > kMaxFrames)
        return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(buffer0);
    const uintptr_t b = reinterpret_cast<uintptr_t>(buffer1);
    const uintptr_t bytes = uintptr_t(capacityFrames) * sizeof(Frame);
    if (a < b + bytes && b < a + bytes)
        return false;  // overlapping storage
    sampleRate_ = sampleRate;
    buffer_[0] = buffer0;
    buffer_[1] = buffer1;
    capacity_ = capacityFrames;
    UpdateCoefficients();
    ready_ = true;
    return true;
}

void Engine::Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                     size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        const float l = inLeft ? inLeft[i] : 0.f;
        const float r = inRight ? inRight[i] : l;
        float ol = 0.f, orr = 0.f;
        if (ready_)
            ProcessSample(l, r, ol, orr);
        if (outLeft)
            outLeft[i] = ol;
        if (outRight)
            outRight[i] = orr;
    }
    if (ready_)
        SyncStep(frames);
}

void Engine::ProcessSample(float inLeft, float inRight, float& outLeft, float& outRight) {
    UpdateSpeed();
    outLeft = outRight = 0.f;
    if (state_ == State::Recording) {
        RecordFirstTake(inLeft, inRight);
    } else {
        WritePostRoll(inLeft, inRight);
        UpdateRamps();
        if (pendingOp_ != Op::None && opGain_ <= 0.f && !overdub_ && recGain_ <= 0.f &&
            postRoll_ >= postRollLength_ &&
            (capture_ == Capture::Off || capture_ == Capture::Waiting))
            ExecuteOperation();
        if (recordRequest_ && CanStartLayer())
            StartLayer();
        if (state_ == State::Looping) {
            if (segmentDirty_)
                UpdateSegmentation();
            if (windowDirty_ && !jumping_)
                ApplyWindow();
            AdvanceSlices();
            UpdateSeamLatches();
            AdvanceHead(inLeft, inRight, outLeft, outRight);
            CaptureSample(outLeft + inLeft, outRight + inRight);  // what is heard
        }
    }
    previousInLeft_ = inLeft;
    previousInRight_ = inRight;
}

// ---------------------------------------------------------------- controls

void Engine::PressRecord() {
    if (!ready_ || importing_)
        return;
    switch (state_) {
    case State::Empty:
        if (pendingOp_ == Op::None)
            StartFirstTake();
        break;
    case State::Recording:
        FinishFirstTake();
        break;
    case State::Looping:
        if (capture_ == Capture::Recording)
            StopCapture();
        else if (capture_ != Capture::Off)
            break;  // post-roll and commit finish by themselves
        else if (overdub_)
            overdub_ = false;
        else if (recordRequest_)
            recordRequest_ = false;
        else if (CanStartLayer())
            StartLayer();
        else
            recordRequest_ = true;  // started as soon as the engine is idle
        break;
    }
}

void Engine::SetPlaying(bool playing) { playing_ = playing; }

void Engine::Undo() {
    if (ready_ && !importing_ && CanUndo() && state_ != State::Recording)
        RequestOperation(Op::Undo);
}

void Engine::Redo() {
    if (ready_ && !importing_ && CanRedo() && state_ != State::Recording)
        RequestOperation(Op::Redo);
}

void Engine::Erase() {
    if (!ready_ || importing_)
        return;
    if (state_ == State::Recording) {  // abandon the first take; nothing to keep
        state_ = State::Empty;
        recordPosition_ = 0;
        ++version_;
        return;
    }
    if (Length() > 0)
        RequestOperation(Op::Erase);
}

void Engine::RequestOperation(Op op) {
    overdub_ = false;  // punch out first; the operation waits for the fade
    recordRequest_ = false;
    capture_ = Capture::Off;  // an unfinished resample is abandoned
    pendingOp_ = op;
}

bool Engine::Busy() const {
    return state_ == State::Recording || overdub_ || recGain_ > 0.f || recordRequest_ ||
           pendingOp_ != Op::None || postRoll_ < postRollLength_ || syncPending_ ||
           capture_ != Capture::Off || importing_;
}

bool Engine::LoadLoop(const Frame* source, uint32_t frames) {
    if (source == nullptr)
        return false;
    Frame* target = BeginImport(frames);
    if (target == nullptr)
        return false;
    std::memcpy(target, source, size_t(frames) * sizeof(Frame));
    return CommitImport(frames, 0, true);
}

Frame* Engine::BeginImport(uint32_t frames) {
    if (!ready_ || frames == 0 || frames > capacity_ || Busy())
        return nullptr;
    importing_ = true;
    importFrames_ = frames;
    return buffer_[1 - active_];
}

bool Engine::CommitImport(uint32_t length, uint32_t tail, bool undoable) {
    if (!importing_)
        return false;
    importing_ = false;
    if (length == 0 || tail > kTailFrames || length + tail > importFrames_ ||
        length + tail > capacity_)
        return false;  // nothing changed; the other buffer may hold a partial file
    const uint8_t target = 1 - active_;
    length_[target] = length;
    tail_[target] = tail;
    active_ = target;
    canUndo_ = undoable;  // the previous state stays in the other buffer
    canRedo_ = false;
    mirror_ = false;
    ++version_;
    EnterLoop(0);
    opGain_ = 0.f;  // fade the new material in
    return true;
}

void Engine::SetVarispeed(float semitones) {
    if (semitones == semitones)
        targetSemitones_ = Clamp(semitones, kMinSemitones, kMaxSemitones);
}

void Engine::SetInertia(float amount) {
    amount = Knob(amount);
    if (amount != inertia_) {
        inertia_ = amount;
        UpdateCoefficients();
    }
}

void Engine::SetInertialSlope(float slope) {
    slope = slope == slope ? Clamp(slope, 0.f, 1.f) : 0.5f;
    if (slope != slope_) {
        slope_ = slope;
        UpdateCoefficients();
    }
}

void Engine::SetStart(float start) {
    start = KnobDeadBand(start);
    if (start != start_) {
        start_ = start;
        windowDirty_ = true;
    }
}

void Engine::SetSize(float size) {
    size = KnobDeadBand(size);
    if (size != size_) {
        size_ = size;
        windowDirty_ = true;
    }
}

void Engine::SetFrippertronicsDecay(float decay) {
    decay_ = Knob(decay);
    retentionTarget_ = kMaxRetention * (1.f - decay_);
}

void Engine::SetSlice(float slice) {
    slice = Knob(slice);
    if (slice != slice_) {
        slice_ = slice;
        segmentDirty_ = true;
    }
}

void Engine::SetSkip(float skip) { skip_ = Knob(skip); }

void Engine::UpdateCoefficients() {
    float s = 2.f * slope_ - 1.f;
    if (std::fabs(s) < kSlopeDeadBand)
        s = 0.f;
    const float weightUp = 1.f - std::max(0.f, -s);
    const float weightDown = 1.f - std::max(0.f, s);
    const float base = 1.998f * inertia_ * inertia_ + 0.002f;  // seconds at weight 1
    auto coefficient = [&](float weight) {
        if (inertia_ < kInertiaOff || weight <= 0.f)
            return 1.f;
        const float tau = base * weight;
        return Clamp(1.f - std::exp(-1.f / (tau * sampleRate_)), 0.f, 1.f);
    };
    coefficientUp_ = coefficient(weightUp);
    coefficientDown_ = coefficient(weightDown);
}

// ---------------------------------------------------------------- observers

uint32_t Engine::Position() const {
    if (state_ == State::Recording)
        return recordPosition_;
    if (state_ == State::Looping)
        return Absolute(head_.index);
    return 0;
}

Frame Engine::StoredFrame(uint32_t index) const {
    if (index >= Length() + TailLength())
        return {};
    return Raw(index);
}

// ---------------------------------------------------------------- speed

void Engine::UpdateSpeed() {
    const float d = targetSemitones_ - glideSemitones_;
    if (d != 0.f) {
        if (std::fabs(d) >= kGlideJumpSemitones)
            glideSemitones_ = targetSemitones_;
        else {
            glideSemitones_ += d * kGlideCoefficient;
            if (std::fabs(targetSemitones_ - glideSemitones_) < 1e-4f)
                glideSemitones_ = targetSemitones_;
        }
        ratio_ = std::exp2(glideSemitones_ / 12.f);
    }
    const float target = playing_ ? ratio_ : 0.f;
    speed_ += (target - speed_) * (target > speed_ ? coefficientUp_ : coefficientDown_);
    if (std::fabs(target - speed_) < 1e-6f)
        speed_ = target;
    if (glideLeft_ > 0) {  // linear slice pitch glide
        pitch_ += pitchStep_;
        if (--glideLeft_ == 0)
            pitch_ = pitchTarget_;
    }
    const float limited = Clamp(speed_ * pitch_, 0.f, kMaxSpeed);
    increment_ += (limited - increment_) * kIncrementCoefficient;
    if (std::fabs(limited - increment_) < 1e-6f)
        increment_ = limited;
}

void Engine::UpdateRamps() {
    const int wanted = reverseRequest_ != sliceReverse_ ? -1 : 1;
    if (wanted != direction_) {
        directionGain_ -= 1.f / float(kReverseRampFrames);
        if (directionGain_ <= 0.f) {
            directionGain_ = 0.f;
            direction_ = wanted;  // flip at silence, then fade back in
        }
    } else if (directionGain_ < 1.f) {
        directionGain_ = std::min(1.f, directionGain_ + 1.f / float(kReverseRampFrames));
    }
    if (pendingOp_ != Op::None)
        opGain_ = std::max(0.f, opGain_ - 1.f / float(kOpRampFrames));
    else if (opGain_ < 1.f)
        opGain_ = std::min(1.f, opGain_ + 1.f / float(kOpRampFrames));
    if (overdub_)
        recGain_ = std::min(1.f, recGain_ + 1.f / float(kPunchFadeFrames));
    else if (recGain_ > 0.f) {
        recGain_ = std::max(0.f, recGain_ - 1.f / float(kPunchFadeFrames));
        if (recGain_ <= 0.f)
            ++version_;  // the layer is complete
    }
    retention_ += (retentionTarget_ - retention_) * kRetentionCoefficient;
    if (std::fabs(retentionTarget_ - retention_) < 1e-6f)
        retention_ = retentionTarget_;
}

// ---------------------------------------------------------------- layers

void Engine::StartFirstTake() {
    ++version_;
    state_ = State::Recording;
    recordPosition_ = 0;
    length_[0] = length_[1] = 0;
    tail_[0] = tail_[1] = 0;
    syncPending_ = false;  // both buffers are rewritten; older states are gone
    mirror_ = false;
    canUndo_ = canRedo_ = false;
    postRoll_ = postRollLength_ = 0;
    overdub_ = recordRequest_ = false;
    recGain_ = 0.f;
    capture_ = Capture::Off;
    jumping_ = false;
    playing_ = true;
}

void Engine::RecordFirstTake(float inLeft, float inRight) {
    if (recordPosition_ >= capacity_) {
        FinishFirstTake();
        return;
    }
    const float g = recordPosition_ < kRecordFadeFrames
                        ? float(recordPosition_) / float(kRecordFadeFrames)
                        : 1.f;
    const Frame f{inLeft * g, inRight * g};
    buffer_[0][recordPosition_] = f;
    buffer_[1][recordPosition_] = f;
    if (++recordPosition_ >= capacity_)
        FinishFirstTake();  // automatic stop at the buffer limit
}

void Engine::FinishFirstTake() {
    const uint32_t length = std::min(recordPosition_, capacity_);
    ++version_;
    if (length == 0) {
        state_ = State::Empty;
        return;
    }
    length_[active_] = length;
    length_[1 - active_] = 0;  // undo returns to the empty state
    tail_[0] = tail_[1] = 0;
    postRoll_ = 0;
    postRollLength_ = capacity_ - length >= kTailFrames ? kTailFrames : 0;
    mirror_ = true;
    canUndo_ = true;
    canRedo_ = false;
    ComputeWindow(windowStart_, windowSize_);
    EnterLoop(windowStart_);
    if (windowStart_ != 0 || direction_ < 0)
        opGain_ = 0.f;  // playback does not begin on the faded-in first frame
}

// After the first take the input keeps running into both buffers for
// kTailFrames, faded out so that head fade-in + tail fade-out is a crossfade.
void Engine::WritePostRoll(float inLeft, float inRight) {
    if (postRoll_ >= postRollLength_)
        return;
    const uint32_t index = length_[active_] + postRoll_;
    if (index < capacity_) {
        const float g = 1.f - float(postRoll_) / float(kTailFrames);
        const Frame f{inLeft * g, inRight * g};
        buffer_[0][index] = f;
        buffer_[1][index] = f;
    }
    if (++postRoll_ >= postRollLength_) {
        tail_[active_] = postRollLength_;
        ++version_;
    }
}

bool Engine::CanStartLayer() const {
    return state_ == State::Looping && pendingOp_ == Op::None && !overdub_ && recGain_ <= 0.f &&
           capture_ == Capture::Off && postRoll_ >= postRollLength_ &&
           !(syncPending_ && active_ != syncSource_);
}

void Engine::StartLayer() {
    if (mode_ == LoopMode::Resample)
        StartCapture();
    else
        StartOverdub();
}

// A new layer: the other buffer becomes the snapshot of the current state.
// Blocks are copied before their first write (copy on write) and the rest in
// the background, so undo never depends on storage.
void Engine::StartOverdub() {
    ++version_;
    const uint8_t other = 1 - active_;
    length_[other] = length_[active_];
    tail_[other] = tail_[active_];
    if (mirror_) {
        mirror_ = false;  // the other buffer already holds identical data
        syncPending_ = false;
    } else {
        syncPending_ = true;
        syncSource_ = active_;
        syncBlocks_ = (length_[active_] + tail_[active_] + kCopyBlockFrames - 1) / kCopyBlockFrames;
        syncCursor_ = 0;
        syncCredit_ = writeCopies_ = 0;
        std::fill_n(copied_, (syncBlocks_ + 31) / 32, 0u);
    }
    canUndo_ = true;
    canRedo_ = false;
    overdub_ = true;
    recordRequest_ = false;
    writeMode_ = mode_;
}

// Resample: what is heard (loop playback plus input) is written into the
// other buffer from frame 0 with its own length; the current loop becomes the
// undo state when the capture is committed.
void Engine::StartCapture() {
    captureTarget_ = 1 - active_;
    // The other buffer is overwritten: the old undo state and any copy into
    // it are gone. The current state is complete in the active buffer here.
    syncPending_ = false;
    mirror_ = false;
    canUndo_ = canRedo_ = false;
    length_[captureTarget_] = 0;
    tail_[captureTarget_] = 0;
    capture_ = Capture::Recording;
    capturePosition_ = 0;
    recordRequest_ = false;
}

void Engine::StopCapture() {
    captureLength_ = capturePosition_;
    if (captureLength_ == 0) {
        capture_ = Capture::Off;
        return;
    }
    capturePost_ = 0;
    capturePostLength_ = capacity_ - captureLength_ >= kTailFrames ? kTailFrames : 0;
    capture_ = capturePostLength_ ? Capture::PostRoll : Capture::Waiting;
    if (capture_ == Capture::Waiting)
        pendingOp_ = Op::Commit;
}

void Engine::CaptureSample(float left, float right) {
    if (capture_ != Capture::Recording && capture_ != Capture::PostRoll)
        return;
    Frame* target = buffer_[captureTarget_];
    const Frame heard = SoftLimit(Frame{left, right});
    if (capture_ == Capture::Recording) {
        const uint32_t i = capturePosition_;
        if (i < capacity_) {
            const float g = i < kRecordFadeFrames ? float(i) / float(kRecordFadeFrames) : 1.f;
            target[i] = Scale(heard, g);
        }
        if (++capturePosition_ >= capacity_)
            StopCapture();  // automatic stop at the buffer limit
        return;
    }
    const uint32_t i = captureLength_ + capturePost_;
    if (i < capacity_)
        target[i] = Scale(heard, 1.f - float(capturePost_) / float(kTailFrames));
    if (++capturePost_ >= capturePostLength_) {
        capture_ = Capture::Waiting;
        pendingOp_ = Op::Commit;
    }
}

void Engine::ExecuteOperation() {
    ++version_;
    const Op op = pendingOp_;
    pendingOp_ = Op::None;
    recordRequest_ = false;
    uint32_t position = Position();
    switch (op) {
    case Op::Undo:
        if (canUndo_) {
            active_ ^= 1;
            canUndo_ = false;
            canRedo_ = true;
        }
        break;
    case Op::Redo:
        if (canRedo_) {
            active_ ^= 1;
            canRedo_ = false;
            canUndo_ = true;
        }
        break;
    case Op::Erase:
        if (Length() > 0) {
            // The erased state becomes the undo state. If it is the copy source
            // it is complete by itself and the pending copy is no longer needed.
            if (syncPending_ && active_ == syncSource_)
                syncPending_ = false;
            active_ ^= 1;
            length_[active_] = 0;
            tail_[active_] = 0;
            canUndo_ = true;
            canRedo_ = false;
        }
        break;
    case Op::Commit:
        if (capture_ == Capture::Waiting) {
            active_ = captureTarget_;
            length_[active_] = captureLength_;
            tail_[active_] = capturePostLength_;
            canUndo_ = true;  // back to the loop that was resampled
            canRedo_ = false;
            capture_ = Capture::Off;
            position = 0;
        }
        break;
    case Op::None:
        break;
    }
    if (Length() > 0) {
        ComputeWindow(windowStart_, windowSize_);
        if (op == Op::Commit)
            position = windowStart_;
        EnterLoop(position < Length() ? position : 0);
    } else {
        state_ = State::Empty;
    }
}

// Starts looping the current state at `absolute` while the output is silent:
// seams, window and slices are set directly, without crossfades.
void Engine::EnterLoop(uint32_t absolute) {
    state_ = State::Looping;
    ComputeWindow(windowStart_, windowSize_);
    playStart_ = windowStart_;
    playSize_ = windowSize_;
    windowDirty_ = false;
    edgeMode_ = seamRequest_;
    tailMode_ = DesiredTailMode();
    jumping_ = false;
    ResetSlices();
    UpdateSegmentation();
    ResetHead(absolute);
}

// ---------------------------------------------------------------- window and seams

void Engine::ComputeWindow(uint32_t& start, uint32_t& size) const {
    const uint32_t length = Length();
    if (length < kMinWindowFrames) {
        start = 0;
        size = length;
        return;
    }
    start = std::min(uint32_t(double(start_) * double(length)), length - 1);
    const double span = double(length - kMinWindowFrames);
    size = std::min(length, kMinWindowFrames + uint32_t(double(size_) * double(size_) * span));
}

void Engine::PlayWindowTarget(uint32_t& start, uint32_t& size) const {
    if (repeating_) {
        start = Absolute(repeatStart_, windowStart_);
        size = repeatSize_;
    } else {
        start = windowStart_;
        size = windowSize_;
    }
}

void Engine::ApplyWindow() {
    uint32_t start, size;
    ComputeWindow(start, size);
    if (start != windowStart_ || size != windowSize_) {
        windowStart_ = start;
        windowSize_ = size;
        repeating_ = false;  // a repeat belongs to the old window
        UpdateSegmentation();
    }
    PlayWindowTarget(start, size);
    if ((start == playStart_ && size == playSize_) || MoveHead(start, size, Absolute(head_.index)))
        windowDirty_ = false;
}

// Puts the head into the window (start, size) at `absolute`. A position change,
// a position outside the window or on a window edge starts a 384-frame
// crossfade from a second head that keeps reading the old window. Returns
// false when that is needed while a crossfade is still running.
bool Engine::MoveHead(uint32_t start, uint32_t size, uint32_t absolute) {
    if (size == 0)
        return false;
    uint32_t relative = RelativeTo(absolute, start);
    bool jump = absolute != Absolute(head_.index);
    if (relative >= size) {
        relative = direction_ > 0 ? 0 : size - 1;
        jump = true;
    }
    // Either window's edge region shapes the read, so leaving or entering one
    // must crossfade too.
    if (size >= kMinWindowFrames && (relative < kSeamFrames || relative + kSeamFrames >= size))
        jump = true;
    if (playSize_ >= kMinWindowFrames &&
        (head_.index < kSeamFrames || head_.index + kSeamFrames >= playSize_))
        jump = true;
    if (jump) {
        if (jumping_)
            return false;
        StartJump();
    }
    playStart_ = start;
    playSize_ = size;
    head_.index = relative;
    if (jump) {
        head_.current = ReadSeam(relative);
        head_.previous = head_.current;
    }
    return true;
}

// The jump head continues the current read (window and seam) and fades out.
void Engine::StartJump() {
    jump_ = head_;
    jumpStart_ = playStart_;
    jumpSize_ = playSize_;
    jumpTail_ = tailMode_;
    jumping_ = true;
    jumpGain_ = 1.f;
}

void Engine::ResetHead(uint32_t absolute) {
    const uint32_t length = Length();
    uint32_t relative = 0;
    if (length > 0) {
        relative = RelativeTo(std::min(absolute, length - 1), playStart_);
        if (relative >= playSize_)
            relative = direction_ > 0 ? 0 : playSize_ - 1;
    }
    head_.index = relative;
    head_.previous = {};
    head_.current = length > 0 ? ReadSeam(relative) : Frame{};
    phase_ = 0.f;
}

// The post-roll crossfade needs the loop to cover the whole tail. Shorter
// loops, and those without a post-roll, gate their seam instead.
Engine::TailMode Engine::DesiredTailMode() const {
    if (seamRequest_ != SeamMode::Crossfade)
        return TailMode::Off;
    if (tail_[active_] >= kTailFrames && Length() >= kTailFrames)
        return TailMode::Tail;
    return Length() >= kMinWindowFrames ? TailMode::Gate : TailMode::Off;
}

// Seam behavior changes only while the head is away from every seam region,
// so a mode switch or a finished post-roll never steps the output. Where the
// tail mode has no such region (loops up to kTailFrames + kSeamFrames, or a
// window that never leaves the seam), it switches under a jump crossfade, but
// only after the edge mode, so a crossfade edge never meets an ungated seam.
void Engine::UpdateSeamLatches() {
    if (jumping_)
        return;
    const uint32_t relative = head_.index;
    const uint32_t size = playSize_;
    if (edgeMode_ != seamRequest_ &&
        (size < kMinWindowFrames || (relative >= kSeamFrames && relative + kSeamFrames < size)))
        edgeMode_ = seamRequest_;
    const TailMode wanted = DesiredTailMode();
    if (tailMode_ != wanted) {
        const uint32_t absolute = Absolute(relative);
        if (absolute >= kTailFrames && absolute + kSeamFrames < Length() &&
            (size < kMinWindowFrames || relative >= kSeamFrames)) {
            tailMode_ = wanted;
        } else if (edgeMode_ == seamRequest_) {
            StartJump();
            tailMode_ = wanted;
            head_.current = ReadSeam(relative);
            head_.previous = head_.current;
        }
    }
}

uint32_t Engine::Absolute(uint32_t relative, uint32_t start) const {
    const uint32_t length = Length();
    if (length == 0)
        return 0;
    uint32_t a = start + relative;
    while (a >= length)
        a -= length;
    return a;
}

uint32_t Engine::RelativeTo(uint32_t absolute, uint32_t start) const {
    return absolute >= start ? absolute - start : absolute + Length() - start;
}

Frame Engine::Raw(uint32_t index) const {
    if (index >= capacity_)
        return {};
    uint8_t b = active_;
    if (syncPending_ && !Copied(index / kCopyBlockFrames))
        b = syncSource_;  // block not copied yet: both states share this data
    return buffer_[b][index];
}

// Stored frame with the physical seam of the first take: in crossfade mode the
// faded-out post-roll is added to the faded-in start, or, without a post-roll,
// both sides of the seam are gated like the original window.
Frame Engine::Composite(uint32_t absolute, TailMode tail) const {
    Frame x = Raw(absolute);
    const uint32_t length = Length();
    if (tail == TailMode::Tail && absolute < kTailFrames && absolute < tail_[active_]) {
        const Frame t = Raw(length + absolute);
        x.left += t.left;
        x.right += t.right;
    } else if (tail == TailMode::Gate && absolute < length) {
        x = Scale(x, EdgeGain(absolute, length));
    }
    return x;
}

Frame Engine::ReadSeam(uint32_t relative, uint32_t start, uint32_t size, TailMode tail) const {
    const Frame x = Composite(Absolute(relative, start), tail);
    if (size < kMinWindowFrames) {
        // Only a loop shorter than the smallest window: unless the post-roll
        // crossfades it, its seam dips over a third of the loop at each end.
        if (tail == TailMode::Tail)
            return x;
        return Scale(x, EdgeGain(relative, size, std::max<uint32_t>(size / 3, 1)));
    }
    if (edgeMode_ == SeamMode::Window)
        return Scale(x, EdgeGain(relative, size));
    const uint32_t length = Length();
    if (relative < kSeamFrames && size < length) {
        // Window seam: blend in from the material that follows the window end.
        uint32_t continuation = start + size + relative;
        while (continuation >= length)
            continuation -= length;
        const float w = float(relative) / float(kSeamFrames);
        return Mix(Composite(continuation, tail), x, w);
    }
    return x;
}

// ---------------------------------------------------------------- Slice/Skip

// Base segment: the whole window below 4 s, otherwise the window divided by the
// smallest power of two that gives segments of at most about 2 s. Slices
// divide a segment by 1, 2, 4, 8 or 16 and are at least 1152 frames long.
void Engine::UpdateSegmentation() {
    segmentDirty_ = false;
    const uint32_t size = windowSize_;
    uint32_t base = size;
    if (size >= kFreeSegmentLimit) {
        const uint32_t needed = size / kSegmentTarget;
        uint32_t parts = 1;
        while (parts < needed)
            parts <<= 1;
        base = size / parts;
    }
    const float scaled = 4.9f * slice_;
    const uint32_t step = std::min<uint32_t>(uint32_t(scaled), 4);
    sliceDivisor_ = 1u << step;
    repeatDepth_ = uint32_t(6.1f * (scaled - float(step)));
    sliceLength_ = std::min<uint32_t>(std::max(base / sliceDivisor_, kMinWindowFrames),
                                      std::max<uint32_t>(size, 1));
    sliceCount_ = std::max<uint32_t>(1, size / std::max<uint32_t>(sliceLength_, 1));
}

void Engine::ResetSlices() {
    repeating_ = sliceReverse_ = sliceOffset_ = false;
    pitch_ = pitchTarget_ = 1.f;
    pitchStep_ = 0.f;
    glideLeft_ = 0;
    sliceCounter_ = 0.0;
}

// The slice counter advances by the distance the head travels.
void Engine::AdvanceSlices() {
    if (sliceLength_ == 0 || increment_ <= 0.f)
        return;
    sliceCounter_ += double(increment_);
    if (sliceCounter_ < double(sliceLength_))
        return;
    sliceCounter_ -= double(sliceLength_);
    if (sliceCounter_ >= double(sliceLength_))
        sliceCounter_ = 0.0;
    SliceBoundary();
}

uint32_t Engine::NextRandom() {
    uint32_t x = random_;  // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_ = x;
    return x;
}

uint32_t Engine::GlideFrames(float fraction) {
    const uint32_t choices = uint32_t(3.99f * fraction) + 1;  // of 0, 1/4, 1/2, 3/4, 1
    return uint32_t(float(Below(choices)) * 0.25f * float(sliceLength_));
}

void Engine::StartPitch(float target, uint32_t glideFrames) {
    ++events_.pitches;
    pitchTarget_ = target;
    if (glideFrames == 0) {
        pitch_ = target;
        glideLeft_ = 0;
        return;
    }
    pitchStep_ = (target - pitch_) / float(glideFrames);
    glideLeft_ = glideFrames;
}

// At each boundary the previous slice's transformations end and new ones are
// drawn (analysis section 7). Repeats loop a short window at the head; Skip
// moves the head, reverses the slice or changes its pitch.
void Engine::SliceBoundary() {
    ++events_.boundaries;
    const bool wasRepeating = repeating_;
    repeating_ = sliceReverse_ = sliceOffset_ = false;
    pitch_ = pitchTarget_ = 1.f;
    glideLeft_ = 0;
    const uint32_t size = windowSize_;
    const uint32_t slice = sliceLength_;
    if (size == 0)
        return;
    uint32_t relative = RelativeTo(Absolute(head_.index), windowStart_);
    if (relative >= size)
        relative = 0;
    bool moved = false;
    if (skip_ > kSkipActive) {
        const float scaled = 5.99f * skip_;
        const uint32_t zone = uint32_t(scaled);
        const float f = scaled - float(zone);
        if (!jumping_ && Chance(kOffsetChance)) {
            if (f >= .5f)  // jump to a random slice boundary
                relative = Below(sliceCount_) * slice;
            else           // move ahead by up to 2 f slice lengths
                relative = uint32_t((uint64_t(relative) +
                                     uint64_t(float(Below(101)) * .01f * 2.f * f * float(slice))) %
                                    size);
            sliceOffset_ = moved = true;
            ++events_.offsets;
        }
        if (zone >= 1 && Chance(.125f + .5f * f)) {
            sliceReverse_ = true;
            ++events_.reverses;
        }
        if (zone >= 2 && Chance(kPitchChance)) {
            if (zone == 2) {  // micro pitch within +-2 semitones
                StartPitch(std::exp2((4.f * f * Uniform() - 2.f) / 12.f), GlideFrames(f));
            } else if (zone == 3) {  // octaves
                static constexpr float kOctaves[] = {1.f, 2.f, .5f, 4.f};
                StartPitch(kOctaves[Below(uint32_t(3.99f * f) + 1)], 0);
            } else {  // semitone jump, in zone 5 sometimes with a glide
                const float semitones = std::floor(24.f * f) * (Chance(.5f) ? 1.f : -1.f);
                const uint32_t glide = zone >= 5 && Chance(f) ? GlideFrames(f) : 0;
                StartPitch(std::exp2(semitones / 12.f), glide);
            }
        }
    }
    if (slice_ > kSliceActive && Chance(kRepeatChance)) {
        const uint32_t j = Below(repeatDepth_ + 1);
        if (j > 0) {
            const uint32_t r = std::min(std::max(slice >> j, kMinWindowFrames), size);
            const bool backwards = reverseRequest_ != sliceReverse_;
            uint32_t from = backwards ? (relative + 1 >= r ? relative + 1 - r : 0) : relative;
            if (from + r > size)
                from = size - r;
            repeating_ = true;
            repeatStart_ = from;
            repeatSize_ = r;
            ++events_.repeats;
        }
    }
    if (moved || repeating_ || wasRepeating) {
        uint32_t start, length;
        PlayWindowTarget(start, length);
        if (!MoveHead(start, length, Absolute(relative, windowStart_)))
            windowDirty_ = true;  // applied once the running crossfade ends
    }
}

// ---------------------------------------------------------------- head

void Engine::AdvanceHead(float inLeft, float inRight, float& outLeft, float& outRight) {
    const float start = phase_;
    phase_ += increment_;
    const uint32_t crossings = uint32_t(phase_);
    if (crossings > 0) {
        phase_ -= float(crossings);
        const float inverse = 1.f / increment_;
        const uint32_t size = playSize_;
        for (uint32_t c = 1; c <= crossings; ++c) {
            // Input at the moment this frame is crossed inside the sample.
            const float t = Clamp((float(c) - start) * inverse, 0.f, 1.f);
            const Frame input{previousInLeft_ + (inLeft - previousInLeft_) * t,
                              previousInRight_ + (inRight - previousInRight_) * t};
            head_.previous = head_.current;
            if (direction_ > 0)
                head_.index = head_.index + 1 >= size ? 0 : head_.index + 1;
            else
                head_.index = head_.index == 0 ? size - 1 : head_.index - 1;
            head_.current = ReadSeam(head_.index);  // playback hears the old layer
            if (recGain_ > 0.f)
                WriteFrame(Absolute(head_.index), input, recGain_);
            if (jumping_) {
                jump_.previous = jump_.current;
                if (direction_ > 0)
                    jump_.index = jump_.index + 1 >= jumpSize_ ? 0 : jump_.index + 1;
                else
                    jump_.index = jump_.index == 0 ? jumpSize_ - 1 : jump_.index - 1;
                jump_.current = ReadSeam(jump_.index, jumpStart_, jumpSize_, jumpTail_);
            }
        }
    }
    Frame y = Mix(head_.previous, head_.current, phase_);
    if (jumping_) {
        const Frame j = Mix(jump_.previous, jump_.current, phase_);
        y = Mix(y, j, jumpGain_);
        jumpGain_ -= 1.f / float(kSeamFrames);
        if (jumpGain_ <= 0.f)
            jumping_ = false;
    }
    const float g = directionGain_ * opGain_;
    outLeft = y.left * g;
    outRight = y.right * g;
}

void Engine::WriteFrame(uint32_t absolute, Frame input, float gain) {
    if (absolute >= capacity_ || absolute >= Length())
        return;
    if (syncPending_ && active_ != syncSource_)
        return;  // unreachable: layers start only once this buffer is complete
    EnsureCopied(absolute);
    Frame& x = buffer_[active_][absolute];
    float keep = 1.f;  // what remains of the old material, also for the post-roll
    if (writeMode_ == LoopMode::Replace) {
        x = Mix(x, input, gain);
        keep = 1.f - gain;
    } else if (writeMode_ == LoopMode::Frippertronics && !sliceOffset_) {
        // old x k + input, soft limited; the whole result, limit included,
        // fades in with the punch.
        keep = 1.f - gain * (1.f - retention_);
        const Frame full = SoftLimit(
            Frame{x.left * retention_ + input.left, x.right * retention_ + input.right});
        x = Mix(x, full, gain);
    } else {  // Sound on Sound, also Frippertronics while a Skip offset is active
        x.left = Clamp(x.left + input.left * gain, -1.f, 1.f);
        x.right = Clamp(x.right + input.right * gain, -1.f, 1.f);
    }
    // The post-roll belongs to the start of the loop and decays with it.
    if (keep < 1.f && absolute < tail_[active_]) {
        const uint32_t t = Length() + absolute;
        if (t < capacity_) {
            EnsureCopied(t);
            buffer_[active_][t] = Scale(buffer_[active_][t], keep);
        }
    }
}

// ---------------------------------------------------------------- undo copy

void Engine::EnsureCopied(uint32_t index) {
    if (!syncPending_)
        return;
    const uint32_t block = index / kCopyBlockFrames;
    if (block < syncBlocks_ && !Copied(block)) {
        CopyBlock(block);
        writeCopies_ += kCopyBlockFrames;
    }
}

void Engine::CopyBlock(uint32_t block) {
    const uint32_t begin = block * kCopyBlockFrames;
    if (begin >= capacity_)
        return;
    const uint32_t count = std::min(kCopyBlockFrames, capacity_ - begin);
    std::memcpy(buffer_[1 - syncSource_] + begin, buffer_[syncSource_] + begin,
                size_t(count) * sizeof(Frame));
    copied_[block >> 5] |= 1u << (block & 31);
    ++copiedBlocks_;
}

void Engine::SyncStep(size_t frames) {
    if (!syncPending_)
        return;
    syncCredit_ += uint32_t(std::min<size_t>(frames, 4096)) * kSyncFramesPerSample;
    // Copies on write share the budget: a callback copies at most the larger of
    // the budget and what its writes needed.
    syncCredit_ -= std::min(syncCredit_, writeCopies_);
    writeCopies_ = 0;
    while (syncCursor_ < syncBlocks_) {
        const bool copy = !Copied(syncCursor_);
        const uint32_t cost = copy ? kCopyBlockFrames : 8;
        if (syncCredit_ < cost)
            break;
        syncCredit_ -= cost;
        if (copy)
            CopyBlock(syncCursor_);
        ++syncCursor_;
    }
    if (syncCursor_ >= syncBlocks_) {
        syncPending_ = false;
        syncCredit_ = 0;
    }
}

}  // namespace looper
