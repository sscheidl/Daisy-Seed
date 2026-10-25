// SPDX-License-Identifier: MIT
// Loop persistence and WAV import on a card, run from the main loop. All file
// I/O happens here, never in the audio callback; the audio side is reached
// only through EngineView (a snapshot) and ImportPort (a request/reply pair).
#pragma once
#include "looper_engine.h"
#include "resample.h"
#include "wav.h"
#include <cstdint>

namespace looper {

// One open file at a time. Paths are complete, e.g. "0:/looper/loop.wav".
class Volume {
  public:
    virtual ~Volume() = default;
    virtual bool Ready() = 0;  // card present and mounted (may mount on demand)
    virtual bool OpenRead(const char* path) = 0;
    virtual bool OpenWrite(const char* path) = 0;  // create or truncate
    virtual uint32_t Size() = 0;
    virtual bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) = 0;
    virtual bool Write(const void* source, uint32_t bytes) = 0;
    virtual bool Close() = 0;                        // false if not flushed
    virtual bool Remove(const char* path) = 0;       // true if it is gone afterwards
    virtual bool Rename(const char* from, const char* to) = 0;
    virtual bool MakeDirectory(const char* path) = 0;  // true if it exists afterwards
    using Visit = void (*)(void* context, const char* name);
    // Visits the regular, non-hidden files of a directory.
    virtual bool List(const char* path, Visit visit, void* context) = 0;
};

// What the audio thread last published about the engine.
struct EngineSnapshot {
    uint32_t version = 0;
    bool idle = false;  // !Engine::Busy(): the active buffer is only read
    const Frame* frames = nullptr;
    uint32_t length = 0, tail = 0;
};
class EngineView {
  public:
    virtual ~EngineView() = default;
    virtual EngineSnapshot Get() = 0;
};

// Engine::BeginImport / CommitImport / CancelImport, executed by the audio
// thread on request.
class ImportPort {
  public:
    enum class Reply : uint8_t { Idle, Pending, Granted, Denied, Done, Failed };
    virtual ~ImportPort() = default;
    virtual void Begin(uint32_t frames) = 0;
    virtual void Commit(uint32_t length, uint32_t tail) = 0;
    virtual void Cancel() = 0;
    virtual Reply Poll(Frame*& buffer) = 0;
};

class LoopStore {
  public:
    enum class Status : uint8_t { NoCard, Idle, Saving, Importing, Error };
    enum class Fault : uint8_t { None, Card, Write, Busy, NoFiles, File };
    static constexpr uint32_t kSaveDelayMs = 2000;   // after the last change
    static constexpr uint32_t kRetryDelayMs = 10000;  // after a failed save
    static constexpr uint32_t kErrorShowMs = 3000;
    static constexpr uint32_t kMaxFiles = 32;
    static constexpr uint32_t kMaxName = 96;
    static constexpr uint32_t kChunksPerService = 4;
    // Input frames converted per Service call for 44.1 kHz files (a few ms).
    static constexpr uint32_t kResampleFramesPerService = 4096;

    // `buffer` is the transfer buffer (DMA-capable on the firmware), at least
    // 4 KB. `root` is the folder, e.g. "0:/looper": loop.wav, loop.tmp and
    // loops/ live there.
    bool Init(Volume* volume, uint8_t* buffer, uint32_t bytes, uint32_t capacity, const char* root);
    // Enables 44.1 kHz files (converted to 48 kHz). `table` holds
    // resample::kTableSize floats and is filled here; on the firmware it lives
    // in SDRAM. Without it, 44.1 kHz files are rejected with
    // wav::Error::SampleRate. Call after Init.
    bool EnableResampling(float* table, uint32_t floats);
    // Before audio starts: loads the saved loop (or a complete temporary file
    // left by a power loss) straight into the engine. Not undoable.
    bool Recall(Engine& engine);
    // Loads the next file of loops/ (sorted by name) the next time Service runs.
    void RequestImport() { importRequested_ = true; }
    void Service(EngineView& view, ImportPort& port, uint32_t nowMs);

    Status GetStatus(uint32_t nowMs) const;
    bool Active() const { return state_ != State::Idle; }
    Fault LastFault() const { return fault_; }
    wav::Error LastFileError() const { return fileError_; }
    const char* LastImportName() const { return name_; }
    uint32_t Saves() const { return saves_; }

  private:
    enum class State : uint8_t { Idle, Saving, ImportGrant, ImportCopy, ImportCommit };
    struct Names {
        char name[kMaxFiles][kMaxName];
        uint32_t count = 0;
    };
    static void Collect(void* context, const char* name);

    void StartSave(const EngineSnapshot& s, uint32_t nowMs);
    void ContinueSave(EngineView& view, uint32_t nowMs);
    void AbortSave();
    void StartImport(ImportPort& port, uint32_t nowMs);
    void ContinueImport(ImportPort& port, uint32_t nowMs);
    void Fail(Fault fault, uint32_t nowMs, wav::Error file = wav::Error::None);
    bool ReadLoop(const char* path, Engine& engine);
    bool CopyFrames(Frame* target, uint32_t from, uint32_t count);
    bool Accepts(wav::Error& error) const;
    bool Converting() const { return info_.sampleRate != wav::kSampleRate; }
    bool StartConversion(Frame* target);
    bool ConvertFrames(uint32_t from, uint32_t count);

    class FileSource : public wav::Source {
      public:
        Volume* volume = nullptr;
        uint32_t Size() const override { return volume->Size(); }
        bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) override {
            return volume->ReadAt(offset, destination, bytes);
        }
    };

    Volume* volume_ = nullptr;
    uint8_t* buffer_ = nullptr;
    uint32_t bytes_ = 0, capacity_ = 0;
    char loopPath_[64] = {}, tempPath_[64] = {}, loopsPath_[64] = {}, rootPath_[48] = {};
    char filePath_[64 + kMaxName] = {};
    char name_[kMaxName] = {};
    FileSource source_;
    State state_ = State::Idle;
    Fault fault_ = Fault::None;
    wav::Error fileError_ = wav::Error::None;
    uint32_t errorAt_ = 0;
    bool errorShown_ = false;
    bool cardReady_ = false;
    Names names_;
    bool importRequested_ = false;
    uint32_t importIndex_ = 0;

    uint32_t seenVersion_ = 0, savedVersion_ = 0, changedAt_ = 0, retryAt_ = 0;
    bool versionKnown_ = false;
    uint32_t saveVersion_ = 0, saveFrames_ = 0, savePosition_ = 0;
    const Frame* saveSource_ = nullptr;
    uint32_t saves_ = 0;

    wav::Info info_;
    const float* resampleTable_ = nullptr;
    resample::Upsampler upsampler_;
    Frame* importTarget_ = nullptr;
    uint32_t importPosition_ = 0;
};

}  // namespace looper
