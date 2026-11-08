// Loads one .dsy package from the card into the SRAM target and commits the start.
// Portable state machine, driven from the main loop in small steps; the hardware
// (SD/FatFs, cache, BootInfo, reset) is reached only through the interfaces below,
// so the complete order of checks and the commit sequence are host-tested.
//
// Contract (docs/CONTRACT.md, section 7): nothing is committed before the whole
// payload has been read, hashed and checked; on any error the target may contain a
// partial image but BootInfo is never written. The commit order is fixed:
// Quiesce() -> Clean() -> Commit().
#pragma once
#include "pm_catalog.h"
#include "pm_image.h"
#include "pm_sha256.h"

namespace pm {

// Program file access (one open file at a time). Main loop only.
class ImageSource {
  public:
    // Opens a catalog path ("PROGRAMS/..." below /DAISY). `file_size` is the exact length.
    virtual bool Open(const char* path, uint32_t& file_size) = 0;
    // Reads up to `len` bytes; returns the count (0 at end of file), negative on error.
    virtual int32_t Read(uint8_t* dst, uint32_t len) = 0;
    virtual void Close() = 0;
    virtual bool CardPresent() = 0;

  protected:
    ~ImageSource() = default;
};

// The 480 KiB image target (0x24000000 on the board, a buffer on the host).
class LoadTarget {
  public:
    virtual uint8_t* Base() = 0;
    virtual uint32_t Capacity() = 0;  // kTargetSize on the board
    // Writes back dirty data-cache lines of [Base, Base+len) and orders the writes.
    // Called once, after Quiesce(), before Commit().
    virtual void Clean(uint32_t len) = 0;

  protected:
    ~LoadTarget() = default;
};

class BootCommit {
  public:
    // Stops everything that could still write memory or needs an orderly end:
    // audio, SD, LED/USB transfers. Called before Clean().
    virtual void Quiesce() = 0;
    // Writes the bootloader's JUMP order for `address` and resets. On the board this
    // does not return; the host fake records the call.
    virtual void Commit(uint32_t address) = 0;

  protected:
    ~BootCommit() = default;
};

enum class LoadState : uint8_t { Idle, Opening, Header, Payload, Verify, Finalize, Committed, Failed };

enum class LoadError : uint8_t {
    None = 0,
    Busy,          // Start() while a load is active
    NoEntry,
    CardAbsent,    // card-detect low before/while loading
    Open,          // file not found or not readable
    FileSize,      // file length is not header + declared payload
    HeaderRead,    // fewer than 160 bytes readable
    Header,        // see LoadStatus::image_error
    Product,       // header product id differs from the catalog id
    HashMismatch,  // header payload hash differs from the catalog hash
    Read,          // read error from the card
    Short,         // end of file before payload_length bytes
    Long,          // more bytes than payload_length
    Hash,          // computed SHA-256 differs from the header
    Vector,        // see LoadStatus::vector_error
    Budget,        // overall time budget exceeded
    Cancelled,
};

const char* LoadStateName(LoadState state);
const char* LoadErrorName(LoadError error);

struct LoadStatus {
    LoadState state = LoadState::Idle;
    LoadError error = LoadError::None;
    ImageError image_error = ImageError::None;
    VectorError vector_error = VectorError::None;
    uint32_t bytes_done = 0;
    uint32_t bytes_total = 0;
    uint8_t slot = 0;
    uint32_t request_id = 0;
    uint32_t started_ms = 0;
};

class Loader {
  public:
    static constexpr uint32_t kChunk = 4096;      // bytes per Step(); 8 sectors
    static constexpr uint32_t kBudgetMs = 60000;  // whole load, from Start()

    Loader(ImageSource& source, LoadTarget& target, BootCommit& commit);

    // Begins loading `entry`. False when a load is already active (status.error = Busy).
    bool Start(const CatalogEntry& entry, uint32_t now_ms, uint32_t request_id);
    // One main-loop step with at most one chunk of card I/O. Returns true while active.
    bool Step(uint32_t now_ms);
    // Stops an active load; the source is closed and nothing is committed.
    void Cancel();
    // Back to Idle after Failed/Committed, keeping nothing of the previous load.
    void Reset();

    bool Active() const;
    const LoadStatus& Status() const { return status_; }
    uint8_t Percent() const;

  private:
    void Fail(LoadError error);
    bool ReadExact(uint8_t* dst, uint32_t len);

    ImageSource& source_;
    LoadTarget& target_;
    BootCommit& commit_;
    LoadStatus status_;
    CatalogEntry entry_;
    ImageHeader header_;
    uint32_t file_size_ = 0;
    bool open_ = false;
    pm_sha256 sha_;
    uint8_t header_bytes_[kHeaderSize];
};

} // namespace pm
