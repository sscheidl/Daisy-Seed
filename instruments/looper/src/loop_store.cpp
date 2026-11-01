// SPDX-License-Identifier: MIT
#include "loop_store.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace looper {
namespace {

char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
int CompareNames(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const char x = Lower(*a), y = Lower(*b);
        if (x != y || x == 0)
            return int(x) - int(y);
    }
}
bool IsWav(const char* name, size_t length) {
    if (length < 5 || name[0] == '.')
        return false;
    const char* e = name + length - 4;
    return e[0] == '.' && Lower(e[1]) == 'w' && Lower(e[2]) == 'a' && Lower(e[3]) == 'v';
}

}  // namespace

bool LoopStore::Init(Volume* volume, uint8_t* buffer, uint32_t bytes, uint32_t capacity,
                     const char* root) {
    *this = LoopStore();
    if (volume == nullptr || buffer == nullptr || bytes < 4096 || root == nullptr ||
        std::strlen(root) + 10 > sizeof(rootPath_))
        return false;
    volume_ = volume;
    buffer_ = buffer;
    bytes_ = bytes;
    capacity_ = capacity;
    std::snprintf(rootPath_, sizeof(rootPath_), "%s", root);
    std::snprintf(loopPath_, sizeof(loopPath_), "%s/loop.wav", root);
    std::snprintf(tempPath_, sizeof(tempPath_), "%s/loop.tmp", root);
    std::snprintf(loopsPath_, sizeof(loopsPath_), "%s/loops", root);
    source_.volume = volume;
    return true;
}

bool LoopStore::EnableResampling(float* table, uint32_t floats) {
    if (table == nullptr || floats < resample::kTableSize)
        return false;
    resample::BuildTable(table);
    resampleTable_ = table;
    return true;
}

// Parse accepts 44.1 kHz; it is usable only with the conversion table.
bool LoopStore::Accepts(wav::Error& error) const {
    if (Converting() && resampleTable_ == nullptr) {
        error = wav::Error::SampleRate;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- recall

bool LoopStore::Recall(Engine& engine) {
    if (volume_ == nullptr || !volume_->Ready())
        return false;
    volume_->MakeDirectory(rootPath_);
    volume_->MakeDirectory(loopsPath_);
    // A complete temporary file means a power loss between delete and rename.
    const bool ok = ReadLoop(loopPath_, engine) || ReadLoop(tempPath_, engine);
    seenVersion_ = savedVersion_ = engine.ContentVersion();
    versionKnown_ = true;
    return ok;
}

bool LoopStore::ReadLoop(const char* path, Engine& engine) {
    if (!volume_->OpenRead(path))
        return false;
    wav::Error e = wav::Parse(source_, capacity_, info_);
    if (e != wav::Error::None || !Accepts(e)) {
        volume_->Close();
        return false;
    }
    Frame* target = engine.BeginImport(info_.outputFrames);
    bool ok = target != nullptr;
    if (ok && Converting())
        ok = StartConversion(target) && ConvertFrames(0, info_.frames) && upsampler_.Finish();
    else if (ok)
        ok = CopyFrames(target, 0, info_.frames);
    volume_->Close();
    if (!ok) {
        engine.CancelImport();
        return false;
    }
    return info_.hasLoop ? engine.CommitImport(info_.loopLength, info_.tailLength, false)
                         : engine.CommitImport(info_.outputFrames, 0, false);
}

// Reads frames [from, from + count) of the open file into target[from...].
bool LoopStore::CopyFrames(Frame* target, uint32_t from, uint32_t count) {
    const uint32_t perChunk = info_.blockAlign ? bytes_ / info_.blockAlign : 0;
    if (perChunk == 0)
        return false;
    for (uint32_t done = 0; done < count;) {
        const uint32_t n = std::min(perChunk, count - done);
        const uint64_t offset = uint64_t(info_.dataOffset) + uint64_t(from + done) * info_.blockAlign;
        if (offset + uint64_t(n) * info_.blockAlign > volume_->Size() ||
            !volume_->ReadAt(uint32_t(offset), buffer_, n * info_.blockAlign))
            return false;
        wav::Decode(info_, buffer_, n, target + from + done);
        done += n;
    }
    return true;
}

// 44.1 kHz: the converter needs the loop's last frames before its first one.
bool LoopStore::StartConversion(Frame* target) {
    const uint32_t count = std::min(info_.frames, resample::kHalf);
    const uint64_t offset = uint64_t(info_.dataOffset) + uint64_t(info_.frames - count) * info_.blockAlign;
    const uint32_t bytes = count * info_.blockAlign;
    if (bytes > bytes_ || offset + bytes > volume_->Size() ||
        !volume_->ReadAt(uint32_t(offset), buffer_, bytes))
        return false;
    Frame last[resample::kHalf];
    wav::Decode(info_, buffer_, count, last);
    return upsampler_.Start(resampleTable_, target, info_.frames, last, count);
}

// Reads input frames [from, from + count) of the open file into the converter.
bool LoopStore::ConvertFrames(uint32_t from, uint32_t count) {
    constexpr uint32_t kPiece = 64;
    const uint32_t perChunk = info_.blockAlign ? bytes_ / info_.blockAlign : 0;
    if (perChunk == 0)
        return false;
    Frame piece[kPiece];
    for (uint32_t done = 0; done < count;) {
        const uint32_t n = std::min(perChunk, count - done);
        const uint64_t offset = uint64_t(info_.dataOffset) + uint64_t(from + done) * info_.blockAlign;
        if (offset + uint64_t(n) * info_.blockAlign > volume_->Size() ||
            !volume_->ReadAt(uint32_t(offset), buffer_, n * info_.blockAlign))
            return false;
        for (uint32_t i = 0; i < n; i += kPiece) {
            const uint32_t m = std::min(kPiece, n - i);
            wav::Decode(info_, buffer_ + size_t(i) * info_.blockAlign, m, piece);
            upsampler_.Push(piece, m);
        }
        done += n;
    }
    return true;
}

// ---------------------------------------------------------------- service

LoopStore::Status LoopStore::GetStatus(uint32_t nowMs) const {
    if (errorShown_ && nowMs - errorAt_ < kErrorShowMs)
        return Status::Error;
    if (state_ == State::Saving)
        return Status::Saving;
    if (state_ != State::Idle)
        return Status::Importing;
    return cardReady_ ? Status::Idle : Status::NoCard;
}

void LoopStore::Service(EngineView& view, ImportPort& port, uint32_t nowMs) {
    if (volume_ == nullptr)
        return;
    const EngineSnapshot s = view.Get();
    if (!versionKnown_) {
        seenVersion_ = savedVersion_ = s.version;
        versionKnown_ = true;
        changedAt_ = nowMs;
    }
    if (s.version != seenVersion_) {
        seenVersion_ = s.version;
        changedAt_ = nowMs;
    }
    cardReady_ = volume_->Ready();
    if (!cardReady_) {
        if (state_ == State::Saving)
            AbortSave();
        else if (state_ != State::Idle) {
            port.Cancel();
            state_ = State::Idle;
        }
        importRequested_ = false;
        return;
    }
    switch (state_) {
    case State::Idle:
        if (importRequested_) {
            importRequested_ = false;
            StartImport(port, nowMs);
        } else if (s.idle && s.version != savedVersion_ &&
                   (saveSoon_ || nowMs - changedAt_ >= kSaveDelayMs) &&
                   int32_t(nowMs - retryAt_) >= 0) {
            saveSoon_ = false;
            StartSave(s, nowMs);
        } else if (s.version == savedVersion_) {
            saveSoon_ = false;  // nothing left to save
        }
        break;
    case State::Saving:
        if (importRequested_) {  // the import replaces the loop anyway
            importRequested_ = false;
            AbortSave();
            StartImport(port, nowMs);
        } else {
            ContinueSave(view, nowMs);
        }
        break;
    default:
        ContinueImport(port, nowMs);
        break;
    }
}

void LoopStore::Fail(Fault fault, uint32_t nowMs, wav::Error file) {
    fault_ = fault;
    fileError_ = file;
    errorAt_ = nowMs;
    errorShown_ = true;
    retryAt_ = nowMs + kRetryDelayMs;
    state_ = State::Idle;
}

// ---------------------------------------------------------------- save

void LoopStore::StartSave(const EngineSnapshot& s, uint32_t nowMs) {
    volume_->MakeDirectory(rootPath_);
    if (s.length == 0) {  // erased: nothing to recall at power-up
        if (volume_->Remove(loopPath_) && volume_->Remove(tempPath_))
            savedVersion_ = s.version;
        else
            Fail(Fault::Write, nowMs);
        return;
    }
    if (s.frames == nullptr || uint64_t(s.length) + s.tail > capacity_ ||
        !volume_->OpenWrite(tempPath_)) {
        Fail(Fault::Write, nowMs);
        return;
    }
    wav::WriteHeader(buffer_, s.length, s.tail);
    if (!volume_->Write(buffer_, wav::kHeaderBytes)) {
        AbortSave();
        Fail(Fault::Write, nowMs);
        return;
    }
    saveVersion_ = s.version;
    saveFrames_ = s.length + s.tail;
    saveSource_ = s.frames;
    savePosition_ = 0;
    state_ = State::Saving;
}

// The audio thread may start changing the loop at any time; every chunk is
// checked against a snapshot taken after it was copied, and any change throws
// the temporary file away. loop.wav is replaced only by a complete file.
void LoopStore::ContinueSave(EngineView& view, uint32_t nowMs) {
    auto unchanged = [&](const EngineSnapshot& s) {
        return s.idle && s.version == saveVersion_ && s.frames == saveSource_ &&
               s.length + s.tail == saveFrames_;
    };
    const uint32_t perChunk = bytes_ / wav::kBytesPerFrame;
    for (uint32_t k = 0; k < kChunksPerService && savePosition_ < saveFrames_; ++k) {
        if (!unchanged(view.Get())) {
            AbortSave();
            return;
        }
        const uint32_t n = std::min(perChunk, saveFrames_ - savePosition_);
        wav::Encode(saveSource_ + savePosition_, n, buffer_);
        if (!unchanged(view.Get())) {
            AbortSave();
            return;
        }
        if (!volume_->Write(buffer_, n * wav::kBytesPerFrame)) {
            AbortSave();
            Fail(Fault::Write, nowMs);
            return;
        }
        savePosition_ += n;
    }
    if (savePosition_ < saveFrames_)
        return;
    state_ = State::Idle;
    if (!volume_->Close()) {
        volume_->Remove(tempPath_);
        Fail(Fault::Write, nowMs);
        return;
    }
    if (!volume_->Remove(loopPath_) || !volume_->Rename(tempPath_, loopPath_)) {
        Fail(Fault::Write, nowMs);
        return;
    }
    savedVersion_ = saveVersion_;
    fault_ = Fault::None;
    ++saves_;
}

void LoopStore::Halt() {
    if (state_ == State::Saving)
        AbortSave();
    saveSoon_ = false;
}

void LoopStore::AbortSave() {
    volume_->Close();
    volume_->Remove(tempPath_);
    state_ = State::Idle;
}

// ---------------------------------------------------------------- import

void LoopStore::Collect(void* context, const char* name) {
    auto* names = static_cast<Names*>(context);
    const size_t length = std::strlen(name);
    if (names->count >= kMaxFiles || length >= kMaxName || !IsWav(name, length))
        return;
    std::memcpy(names->name[names->count++], name, length + 1);
}

void LoopStore::StartImport(ImportPort& port, uint32_t nowMs) {
    names_.count = 0;
    volume_->MakeDirectory(loopsPath_);
    if (!volume_->List(loopsPath_, &LoopStore::Collect, &names_)) {
        Fail(Fault::Card, nowMs);
        return;
    }
    if (names_.count == 0) {
        Fail(Fault::NoFiles, nowMs);
        return;
    }
    for (uint32_t i = 1; i < names_.count; ++i)  // insertion sort, ignoring case
        for (uint32_t j = i; j > 0 && CompareNames(names_.name[j], names_.name[j - 1]) < 0; --j)
            std::swap(names_.name[j], names_.name[j - 1]);
    std::memcpy(name_, names_.name[importIndex_ % names_.count], kMaxName);
    ++importIndex_;
    std::snprintf(filePath_, sizeof(filePath_), "%s/%s", loopsPath_, name_);
    if (!volume_->OpenRead(filePath_)) {
        Fail(Fault::File, nowMs, wav::Error::Io);
        return;
    }
    wav::Error e = wav::Parse(source_, capacity_, info_);
    if (e != wav::Error::None || !Accepts(e)) {
        volume_->Close();
        Fail(Fault::File, nowMs, e);
        return;
    }
    port.Begin(info_.outputFrames);
    state_ = State::ImportGrant;
}

void LoopStore::ContinueImport(ImportPort& port, uint32_t nowMs) {
    Frame* buffer = nullptr;
    const ImportPort::Reply reply = port.Poll(buffer);
    switch (state_) {
    case State::ImportGrant:
        if (reply == ImportPort::Reply::Granted && buffer != nullptr) {
            importTarget_ = buffer;
            importPosition_ = 0;
            state_ = State::ImportCopy;
            if (Converting() && !StartConversion(buffer)) {
                volume_->Close();
                port.Cancel();
                Fail(Fault::File, nowMs, wav::Error::Io);
            }
        } else if (reply != ImportPort::Reply::Pending) {
            volume_->Close();  // recording, resampling or another change running
            Fail(Fault::Busy, nowMs);
        }
        break;
    case State::ImportCopy: {
        const uint32_t budget = Converting() ? kResampleFramesPerService
                                             : bytes_ / info_.blockAlign * kChunksPerService;
        const uint32_t n = std::min(budget, info_.frames - importPosition_);
        const bool copied = Converting() ? ConvertFrames(importPosition_, n)
                                         : CopyFrames(importTarget_, importPosition_, n);
        if (!copied) {
            volume_->Close();
            port.Cancel();
            Fail(Fault::File, nowMs, wav::Error::Io);
            break;
        }
        importPosition_ += n;
        if (importPosition_ >= info_.frames) {
            volume_->Close();
            if (Converting() && !upsampler_.Finish()) {
                port.Cancel();
                Fail(Fault::File, nowMs, wav::Error::Io);
                break;
            }
            if (info_.hasLoop)
                port.Commit(info_.loopLength, info_.tailLength);
            else
                port.Commit(info_.outputFrames, 0);
            state_ = State::ImportCommit;
        }
        break;
    }
    case State::ImportCommit:
        if (reply == ImportPort::Reply::Done) {
            state_ = State::Idle;
            fault_ = Fault::None;
        } else if (reply != ImportPort::Reply::Pending) {
            Fail(Fault::File, nowMs);
        }
        break;
    default:
        break;
    }
}

}  // namespace looper
