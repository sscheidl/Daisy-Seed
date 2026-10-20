// SPDX-License-Identifier: MIT
// FatFS on the Seed3 microSD slot through libDaisy's SDMMC driver (4-bit,
// IDMA). The SDMMC1 IDMA cannot reach DTCM, where .bss lives in this build,
// so the FatFS objects sit in SDRAM; the transfer buffer is placed there by
// main.cpp. libDaisy's driver cleans and invalidates the D-cache around each
// transfer; both objects are 32-byte aligned and padded so no other data
// shares their cache lines.
#include "card_volume.h"
#include "daisy_seed.h"
#include <cstring>
#include <new>

namespace looper {
namespace {

constexpr size_t Lines(size_t bytes) { return (bytes + 31) & ~size_t(31); }
alignas(32) unsigned char DSY_SDRAM_BSS fsStorage[Lines(sizeof(daisy::FatFSInterface))];
alignas(32) unsigned char DSY_SDRAM_BSS fileStorage[Lines(sizeof(FIL))];
constexpr uint32_t kMountRetryMs = 1000;

class FatFsVolume final : public Volume {
  public:
    bool Init(bool (*present)()) {
        present_ = present;
        // .sdram_bss is NOLOAD: clear it before constructing anything there.
        std::memset(fsStorage, 0, sizeof(fsStorage));
        std::memset(fileStorage, 0, sizeof(fileStorage));
        fs_ = new (fsStorage) daisy::FatFSInterface();
        file_ = new (fileStorage) FIL();
        daisy::SdmmcHandler::Config config;
        config.Defaults();
        config.speed = daisy::SdmmcHandler::Speed::STANDARD;
        if (sd_.Init(config) != daisy::SdmmcHandler::Result::OK)
            return false;
        initialized_ = fs_->Init(daisy::FatFSInterface::Config::MEDIA_SD) ==
                       daisy::FatFSInterface::Result::OK;
        return initialized_;
    }

    bool Ready() override {
        if (!initialized_)
            return false;
        if (present_ == nullptr || !present_()) {
            Unmount();
            return false;
        }
        if (mounted_)
            return true;
        const uint32_t now = daisy::System::GetNow();
        if (tried_ && now - lastTry_ < kMountRetryMs)
            return false;
        tried_ = true;
        lastTry_ = now;
        mounted_ = f_mount(&fs_->GetSDFileSystem(), fs_->GetSDPath(), 1) == FR_OK;
        if (mounted_)  // HAL_SD_MspInit set priority 0: stay below the audio DMA
            HAL_NVIC_SetPriority(SDMMC1_IRQn, 2, 0);
        return mounted_;
    }
    bool OpenRead(const char* path) override { return Open(path, FA_READ); }
    bool OpenWrite(const char* path) override { return Open(path, FA_WRITE | FA_CREATE_ALWAYS); }
    uint32_t Size() override { return open_ ? uint32_t(f_size(file_)) : 0; }
    bool ReadAt(uint32_t offset, void* destination, uint32_t bytes) override {
        UINT got = 0;
        if (!open_ || !Check(f_lseek(file_, offset)) || !Check(f_read(file_, destination, bytes, &got)))
            return false;
        return got == bytes;
    }
    bool Write(const void* source, uint32_t bytes) override {
        UINT put = 0;
        return open_ && Check(f_write(file_, source, bytes, &put)) && put == bytes;
    }
    bool Close() override {
        if (!open_)
            return false;
        open_ = false;
        return Check(f_close(file_));
    }
    bool Remove(const char* path) override {
        if (!mounted_)
            return false;
        const FRESULT r = f_unlink(path);
        if (r == FR_OK || r == FR_NO_FILE || r == FR_NO_PATH)
            return true;
        return Check(r);  // false; unmounts after a disk error
    }
    bool Rename(const char* from, const char* to) override {
        return mounted_ && Check(f_rename(from, to));
    }
    bool MakeDirectory(const char* path) override {
        if (!mounted_)
            return false;
        const FRESULT r = f_mkdir(path);
        if (r == FR_OK || r == FR_EXIST)
            return true;
        return Check(r);
    }
    bool List(const char* path, Visit visit, void* context) override {
        if (!mounted_)
            return false;
        DIR dir;
        FILINFO info;
        if (!Check(f_opendir(&dir, path)))
            return false;
        bool ok = true;
        for (;;) {
            if (!Check(f_readdir(&dir, &info))) {
                ok = false;
                break;
            }
            if (info.fname[0] == 0)
                break;
            if (!(info.fattrib & (AM_DIR | AM_HID | AM_SYS)))
                visit(context, info.fname);
        }
        f_closedir(&dir);
        return ok;
    }

  private:
    bool Open(const char* path, BYTE mode) {
        if (!mounted_ || open_)
            return false;
        open_ = Check(f_open(file_, path, mode));
        return open_;
    }
    // Low-level errors usually mean the card went away: mount again later.
    bool Check(FRESULT r) {
        if (r == FR_DISK_ERR || r == FR_NOT_READY || r == FR_INT_ERR || r == FR_INVALID_OBJECT)
            Unmount();
        return r == FR_OK;
    }
    void Unmount() {
        if (!mounted_)
            return;
        if (open_)
            f_close(file_);
        open_ = false;
        f_mount(nullptr, fs_->GetSDPath(), 0);
        mounted_ = false;
        tried_ = false;
    }

    bool (*present_)() = nullptr;
    daisy::SdmmcHandler sd_;
    daisy::FatFSInterface* fs_ = nullptr;
    FIL* file_ = nullptr;
    bool initialized_ = false, mounted_ = false, open_ = false, tried_ = false;
    uint32_t lastTry_ = 0;
};

FatFsVolume volume;

}  // namespace

bool InitCard(bool (*present)()) { return volume.Init(present); }
Volume& CardVolume() { return volume; }

}  // namespace looper
