// SPDX-License-Identifier: MIT
#include "looper_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace looper {
namespace {

constexpr float kGlideCoefficient = 0.001f;   // one-pole per sample, about 21 ms
constexpr float kGlideJumpSemitones = 4.f;    // larger target changes are immediate
constexpr float kIncrementCoefficient = 0.1f; // head increment smoothing per sample
constexpr float kInertiaOff = 0.03f;
constexpr float kSlopeDeadBand = 0.05f;
constexpr float kKnobDeadBand = 0.003f;

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
// Linear gain over the first and last kSeamFrames of a span; exactly 0 at both ends.
float EdgeGain(uint32_t index, uint32_t span) {
    float g = 1.f;
    if (index < kSeamFrames)
        g = float(index) / float(kSeamFrames);
    const uint32_t back = span - 1 - index;
    if (back < kSeamFrames)
        g = std::min(g, float(back) / float(kSeamFrames));
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
            postRoll_ >= postRollLength_)
            ExecuteOperation();
        if (recordRequest_ && CanStartOverdub())
            StartOverdub();
        if (state_ == State::Looping) {
            if (windowDirty_ && !jumping_)
                ApplyWindow();
            UpdateSeamLatches();
            AdvanceHead(inLeft, inRight, outLeft, outRight);
        }
    }
    previousInLeft_ = inLeft;
    previousInRight_ = inRight;
}

// ---------------------------------------------------------------- controls

void Engine::PressRecord() {
    if (!ready_)
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
        if (overdub_)
            overdub_ = false;
        else if (recordRequest_)
            recordRequest_ = false;
        else if (CanStartOverdub())
            StartOverdub();
        else
            recordRequest_ = true;  // started as soon as the engine is idle
        break;
    }
}

void Engine::SetPlaying(bool playing) { playing_ = playing; }

void Engine::Undo() {
    if (ready_ && canUndo_ && state_ != State::Recording)
        RequestOperation(Op::Undo);
}

void Engine::Redo() {
    if (ready_ && canRedo_ && state_ != State::Recording)
        RequestOperation(Op::Redo);
}

void Engine::Erase() {
    if (!ready_)
        return;
    if (state_ == State::Recording) {  // abandon the first take; nothing to keep
        state_ = State::Empty;
        recordPosition_ = 0;
        return;
    }
    if (Length() > 0)
        RequestOperation(Op::Erase);
}

void Engine::RequestOperation(Op op) {
    overdub_ = false;  // punch out first; the operation waits for the fade
    recordRequest_ = false;
    pendingOp_ = op;
}

bool Engine::Busy() const {
    return state_ == State::Recording || overdub_ || recGain_ > 0.f || recordRequest_ ||
           pendingOp_ != Op::None || postRoll_ < postRollLength_ || syncPending_;
}

bool Engine::LoadLoop(const Frame* source, uint32_t frames) {
    if (!ready_ || source == nullptr || frames == 0 || frames > capacity_ || Busy())
        return false;
    const uint8_t target = 1 - active_;
    std::memcpy(buffer_[target], source, size_t(frames) * sizeof(Frame));
    length_[target] = frames;
    tail_[target] = 0;
    active_ = target;
    canUndo_ = true;  // the previous state stays in the other buffer
    canRedo_ = false;
    mirror_ = false;
    state_ = State::Looping;
    ComputeWindow(windowStart_, windowSize_);
    windowDirty_ = false;
    edgeMode_ = seamRequest_;
    tailMode_ = DesiredTailMode();
    jumping_ = false;
    ResetHead(0);
    opGain_ = 0.f;  // fade the new material in
    return true;
}

void Engine::SetVarispeed(float semitones) {
    if (semitones == semitones)
        targetSemitones_ = Clamp(semitones, kMinSemitones, kMaxSemitones);
}

void Engine::SetInertia(float amount) {
    amount = amount == amount ? Clamp(amount, 0.f, 1.f) : 0.f;
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
    const float limited = Clamp(speed_, 0.f, kMaxSpeed);
    increment_ += (limited - increment_) * kIncrementCoefficient;
    if (std::fabs(limited - increment_) < 1e-6f)
        increment_ = limited;
}

void Engine::UpdateRamps() {
    const int wanted = reverseRequest_ ? -1 : 1;
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
    else if (recGain_ > 0.f)
        recGain_ = std::max(0.f, recGain_ - 1.f / float(kPunchFadeFrames));
}

// ---------------------------------------------------------------- layers

void Engine::StartFirstTake() {
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
    state_ = State::Looping;
    ComputeWindow(windowStart_, windowSize_);
    windowDirty_ = false;
    edgeMode_ = seamRequest_;
    tailMode_ = DesiredTailMode();
    jumping_ = false;
    ResetHead(windowStart_);
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
    if (++postRoll_ >= postRollLength_)
        tail_[active_] = postRollLength_;
}

bool Engine::CanStartOverdub() const {
    return state_ == State::Looping && pendingOp_ == Op::None && !overdub_ && recGain_ <= 0.f &&
           postRoll_ >= postRollLength_ && !(syncPending_ && active_ != syncSource_);
}

// A new layer: the other buffer becomes the snapshot of the current state.
// Blocks are copied before their first write (copy on write) and the rest in
// the background, so undo never depends on storage.
void Engine::StartOverdub() {
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
        syncCredit_ = 0;
        std::fill_n(copied_, (syncBlocks_ + 31) / 32, 0u);
    }
    canUndo_ = true;
    canRedo_ = false;
    overdub_ = true;
    recordRequest_ = false;
    writeMode_ = mode_;
}

void Engine::ExecuteOperation() {
    const Op op = pendingOp_;
    pendingOp_ = Op::None;
    recordRequest_ = false;
    const uint32_t position = Position();
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
    case Op::None:
        break;
    }
    state_ = Length() > 0 ? State::Looping : State::Empty;
    if (state_ == State::Looping) {
        ComputeWindow(windowStart_, windowSize_);
        windowDirty_ = false;
        edgeMode_ = seamRequest_;
        tailMode_ = DesiredTailMode();
        jumping_ = false;
        ResetHead(position < Length() ? position : 0);
    }
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

void Engine::ApplyWindow() {
    windowDirty_ = false;
    uint32_t start, size;
    ComputeWindow(start, size);
    if (start == windowStart_ && size == windowSize_)
        return;
    const uint32_t length = Length();
    const uint32_t absolute = Absolute(head_.index);
    const uint32_t oldStart = windowStart_, oldSize = windowSize_, oldIndex = head_.index;
    windowStart_ = start;
    windowSize_ = size;
    const uint32_t relative = absolute >= start ? absolute - start : absolute + length - start;
    const bool edge = size >= kMinWindowFrames &&
                      (relative < kSeamFrames || relative + kSeamFrames >= size);
    if (relative < size && !edge) {
        head_.index = relative;
        return;
    }
    // Crossfade from a second head that keeps reading the old window.
    jump_.index = oldIndex;
    jumpStart_ = oldStart;
    jumpSize_ = oldSize;
    jump_.previous = head_.previous;
    jump_.current = head_.current;
    jumping_ = true;
    jumpGain_ = 1.f;
    head_.index = relative < size ? relative : (direction_ > 0 ? 0 : size - 1);
    head_.current = ReadSeam(head_.index);
    head_.previous = head_.current;
}

void Engine::ResetHead(uint32_t absolute) {
    const uint32_t length = Length();
    uint32_t relative = 0;
    if (length > 0) {
        absolute = std::min(absolute, length - 1);
        relative = absolute >= windowStart_ ? absolute - windowStart_
                                            : absolute + length - windowStart_;
        if (relative >= windowSize_)
            relative = direction_ > 0 ? 0 : windowSize_ - 1;
    }
    head_.index = relative;
    head_.previous = {};
    head_.current = length > 0 ? ReadSeam(relative) : Frame{};
    phase_ = 0.f;
}

Engine::TailMode Engine::DesiredTailMode() const {
    if (seamRequest_ != SeamMode::Crossfade || Length() < kMinWindowFrames)
        return TailMode::Off;
    return tail_[active_] >= kTailFrames ? TailMode::Tail : TailMode::Gate;
}

// Seam behavior changes only while the head is away from every seam region,
// so a mode switch or a finished post-roll never steps the output.
void Engine::UpdateSeamLatches() {
    if (jumping_)
        return;
    const uint32_t relative = head_.index;
    const uint32_t size = windowSize_;
    if (edgeMode_ != seamRequest_ &&
        (size < kMinWindowFrames || (relative >= kSeamFrames && relative + kSeamFrames < size)))
        edgeMode_ = seamRequest_;
    const TailMode wanted = DesiredTailMode();
    if (tailMode_ != wanted) {
        const uint32_t absolute = Absolute(relative);
        if (absolute >= kTailFrames && absolute + kSeamFrames < Length() &&
            (size < kMinWindowFrames || relative >= kSeamFrames))
            tailMode_ = wanted;
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
Frame Engine::Composite(uint32_t absolute) const {
    Frame x = Raw(absolute);
    const uint32_t length = Length();
    if (tailMode_ == TailMode::Tail && absolute < kTailFrames && absolute < tail_[active_]) {
        const Frame t = Raw(length + absolute);
        x.left += t.left;
        x.right += t.right;
    } else if (tailMode_ == TailMode::Gate && absolute < length) {
        x = Scale(x, EdgeGain(absolute, length));
    }
    return x;
}

Frame Engine::ReadSeam(uint32_t relative, uint32_t start, uint32_t size) const {
    const Frame x = Composite(Absolute(relative, start));
    if (size < kMinWindowFrames)
        return x;
    if (edgeMode_ == SeamMode::Window)
        return Scale(x, EdgeGain(relative, size));
    const uint32_t length = Length();
    if (relative < kSeamFrames && size < length) {
        // Window seam: blend in from the material that follows the window end.
        uint32_t continuation = start + size + relative;
        while (continuation >= length)
            continuation -= length;
        const float w = float(relative) / float(kSeamFrames);
        return Mix(Composite(continuation), x, w);
    }
    return x;
}

// ---------------------------------------------------------------- head

void Engine::AdvanceHead(float inLeft, float inRight, float& outLeft, float& outRight) {
    const float start = phase_;
    phase_ += increment_;
    const uint32_t crossings = uint32_t(phase_);
    if (crossings > 0) {
        phase_ -= float(crossings);
        const float inverse = 1.f / increment_;
        const uint32_t size = windowSize_;
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
                jump_.current = ReadSeam(jump_.index, jumpStart_, jumpSize_);
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
    if (writeMode_ == LoopMode::SoundOnSound) {
        x.left = Clamp(x.left + input.left * gain, -1.f, 1.f);
        x.right = Clamp(x.right + input.right * gain, -1.f, 1.f);
        return;
    }
    x = Mix(x, input, gain);
    // The post-roll belongs to the start of the loop and is replaced with it.
    if (absolute < tail_[active_]) {
        const uint32_t t = Length() + absolute;
        if (t < capacity_) {
            EnsureCopied(t);
            buffer_[active_][t] = Scale(buffer_[active_][t], 1.f - gain);
        }
    }
}

// ---------------------------------------------------------------- undo copy

void Engine::EnsureCopied(uint32_t index) {
    if (!syncPending_)
        return;
    const uint32_t block = index / kCopyBlockFrames;
    if (block < syncBlocks_ && !Copied(block))
        CopyBlock(block);
}

void Engine::CopyBlock(uint32_t block) {
    const uint32_t begin = block * kCopyBlockFrames;
    if (begin >= capacity_)
        return;
    const uint32_t count = std::min(kCopyBlockFrames, capacity_ - begin);
    std::memcpy(buffer_[1 - syncSource_] + begin, buffer_[syncSource_] + begin,
                size_t(count) * sizeof(Frame));
    copied_[block >> 5] |= 1u << (block & 31);
}

void Engine::SyncStep(size_t frames) {
    if (!syncPending_)
        return;
    syncCredit_ += uint32_t(std::min<size_t>(frames, 4096)) * kSyncFramesPerSample;
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
