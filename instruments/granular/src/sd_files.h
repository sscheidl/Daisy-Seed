// FatFs R0.16 (common/sd, read-only) adapters for the bank scanner and the WAV loader.
// Main loop only. Compiled into the firmware and into the host end-to-end test.
#pragma once
#include "banks.h"
#include "wav.h"
#include "ff.h"

namespace neb {

class FatDirectory final : public Directory {
  public:
    bool Open(const char* path) override;
    int Next(DirEntry& entry) override;
    void Close() override;
    bool IsOpen() const { return open_; }

  private:
    DIR dir_{};
    FILINFO info_{};
    bool open_ = false;
};

// Opens slot N of the bank whose file list the scanner currently holds.
class FatFile final : public File {
  public:
    // `present` is the card-detect switch; `failed` is called on any I/O error so the
    // main loop can remount (the volume is invalid until then).
    FatFile(const BankScanner& banks, bool (*present)(), void (*failed)())
        : banks_(banks), present_(present), failed_(failed) {}
    // Set by the main loop: false until a mount succeeded and while unmounted.
    void SetMounted(bool mounted) { mounted_ = mounted; if(!mounted) open_ = false; }
    FileError Open(unsigned slot) override;
    uint32_t Size() const override { return open_ ? uint32_t(f_size(&handle_)) : 0; }
    bool Read(uint32_t offset, void* dst, uint32_t bytes) override;
    void Close() override;
    unsigned Bank() const { return bank_; }

  private:
    const BankScanner& banks_;
    bool (*present_)();
    void (*failed_)();
    FIL handle_{};
    bool open_ = false, mounted_ = false;
    unsigned bank_ = 0;
};

} // namespace neb
