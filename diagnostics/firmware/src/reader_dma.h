// Bounded, main-loop-only DMA transfer logic. Backend is HAL on the board and
// a deterministic fake in host tests. Buffer must be 32-byte aligned and IDMA reachable.
#pragma once
#include <cstdint>
#include <cstring>

namespace diag {
class ReaderDmaBackend {
  public:
    virtual bool Available() = 0;
    virtual uint32_t Now() = 0;
    virtual bool StartRead(uint8_t* data, uint32_t lba) = 0;
    virtual bool StartWrite(uint8_t* data, uint32_t lba) = 0;
    virtual bool Busy() = 0;
    virtual uint32_t Error() = 0;
    virtual bool CardReady() = 0;
    virtual void Clean(uint8_t* data) = 0;
    virtual void Invalidate(uint8_t* data) = 0;
    virtual void Abort() = 0;
    virtual ~ReaderDmaBackend() = default;
};
enum class ReaderIoFault : uint8_t { None, Unavailable, Start, Dma, DmaTimeout, ProgramTimeout, Alignment };
struct ReaderIoStatus {
    ReaderIoFault fault = ReaderIoFault::None;
    uint32_t hal_error = 0, lba = 0, elapsed_ms = 0;
    const char* operation = "NONE";
};
class ReaderDma {
  public:
    static constexpr uint32_t kBytes = 512, kTimeoutMs = 1000;
    ReaderDma(ReaderDmaBackend& backend, uint8_t* buffer) : b_(backend), buffer_(buffer) {}
    void ResetStatus() { status_ = {}; }
    const ReaderIoStatus& Status() const { return status_; }
    bool Read(uint32_t lba, uint8_t* destination) {
        Begin("READ", lba);
        if(!Validate()) return false;
        // Entire cache lines exclusively belong to this buffer. Clean before
        // invalidation so no dirty line can later overwrite IDMA's new bytes.
        b_.Clean(buffer_); b_.Invalidate(buffer_);
        if(!b_.StartRead(buffer_, lba)) return Fail(ReaderIoFault::Start);
        if(!WaitDma() || !WaitProgramming()) return false;
        b_.Invalidate(buffer_);
        std::memcpy(destination, buffer_, kBytes);
        return true;
    }
    bool Write(uint32_t lba, const uint8_t* source) {
        Begin("WRITE", lba);
        if(!Validate()) return false;
        std::memcpy(buffer_, source, kBytes);
        b_.Clean(buffer_);
        if(!b_.StartWrite(buffer_, lba)) return Fail(ReaderIoFault::Start);
        return WaitDma() && WaitProgramming(); // DMA completion alone is not programming completion
    }
    bool Sync() {
        Begin("SYNC", 0);
        return WaitProgramming();
    }
  private:
    bool WaitProgramming() {
        const uint32_t start = b_.Now();
        while(true) {
            if(!b_.Available()) return Fail(ReaderIoFault::Unavailable);
            if(b_.Error()) return Fail(ReaderIoFault::Dma);
            if(b_.CardReady()) return true;
            if(b_.Now() - start >= kTimeoutMs) return Fail(ReaderIoFault::ProgramTimeout);
        }
    }
    void Begin(const char* operation, uint32_t lba) {
        status_ = {}; status_.operation = operation; status_.lba = lba;
        started_ = b_.Now();
    }
    bool Validate() {
        if(!buffer_ || (reinterpret_cast<uintptr_t>(buffer_) & 31u) != 0) return Fail(ReaderIoFault::Alignment);
        return b_.Available() || Fail(ReaderIoFault::Unavailable);
    }
    bool Fail(ReaderIoFault fault) {
        status_.fault = fault;
        status_.hal_error = b_.Error(); // capture BEFORE HAL_SD_Abort clears the error
        status_.elapsed_ms = b_.Now() - started_;
        b_.Abort();
        return false;
    }
    bool WaitDma() {
        const uint32_t start = b_.Now();
        while(b_.Busy()) {
            if(!b_.Available()) return Fail(ReaderIoFault::Unavailable);
            if(b_.Now() - start >= kTimeoutMs) return Fail(ReaderIoFault::DmaTimeout);
        }
        if(!b_.Available()) return Fail(ReaderIoFault::Unavailable);
        return !b_.Error() || Fail(ReaderIoFault::Dma);
    }
    ReaderDmaBackend& b_;
    uint8_t* buffer_;
    uint32_t started_ = 0;
    ReaderIoStatus status_;
};
} // namespace diag
