// SPDX-License-Identifier: MIT
// looper::Volume on the shared writable SD driver (common/sd, FatFs R0.16 with exFAT,
// built with rw/ffconf.h). Portable: no libDaisy, host-tested on FAT32/exFAT images
// (tests/test_card.cpp). The board wiring (SDMMC init, bounce buffer, abort) is in
// fatfs_volume.cpp. Main loop only.
#pragma once
#include "loop_store.h"
#include "ff.h"

namespace looper {

class FatVolume final : public Volume {
  public:
    static constexpr uint32_t kMountRetryMs = 1000;
    // `present`: card-detect switch; `now`: millisecond clock. ro_sd_init must have run.
    void Init(bool (*present)(), uint32_t (*now)());
    bool Ready() override;
    bool OpenRead(const char* path) override { return Open(path, FA_READ); }
    bool OpenWrite(const char* path) override { return Open(path, FA_WRITE | FA_CREATE_ALWAYS); }
    uint32_t Size() override;
    bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) override;
    bool Write(const void* source, uint32_t bytes) override;
    bool Close() override;
    bool Remove(const char* path) override;
    bool Rename(const char* from, const char* to) override;
    bool MakeDirectory(const char* path) override;
    bool List(const char* path, Visit visit, void* context) override;
    // Closes the open file and unregisters the volume (card removed, SD sharing).
    void Unmount();
    bool Mounted() const { return mounted_; }

  private:
    bool Open(const char* path, BYTE mode);
    bool Check(FRESULT r);
    bool (*present_)() = nullptr;
    uint32_t (*now_)() = nullptr;
    FATFS fs_{};
    FIL file_{};
    bool initialized_ = false, mounted_ = false, open_ = false, tried_ = false;
    uint32_t lastTry_ = 0;
};

}  // namespace looper
