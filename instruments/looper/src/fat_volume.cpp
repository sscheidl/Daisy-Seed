// SPDX-License-Identifier: MIT
#include "fat_volume.h"
#include "ro_sd.h"

// The headers must match the linked objects: common/sd's FatFs R0.16, writable profile.
#if !defined(RO_SD_FFCONF) || FF_DEFINED != 80386 || FF_FS_READONLY != 0 || FF_FS_EXFAT != 1
#error "FatVolume needs common/sd's writable profile (SD_WRITABLE = 1, rw/ffconf.h)"
#endif

namespace looper {

void FatVolume::Init(bool (*present)(), uint32_t (*now)()) {
    present_ = present;
    now_ = now;
    initialized_ = true;
}

bool FatVolume::Ready() {
    if (!initialized_)
        return false;
    if (present_ == nullptr || !present_()) {
        Unmount();
        return false;
    }
    if (mounted_)
        return true;
    const uint32_t now = now_();
    if (tried_ && now - lastTry_ < kMountRetryMs)
        return false;
    tried_ = true;
    lastTry_ = now;
    // ro_sd_mount re-initializes the card interface every time (swap, earlier error).
    mounted_ = ro_sd_mount(&fs_) == FR_OK;
    return mounted_;
}

uint32_t FatVolume::Size() { return open_ ? uint32_t(f_size(&file_)) : 0; }

bool FatVolume::ReadAt(uint32_t offset, void* destination, uint32_t bytes) {
    UINT got = 0;
    if (!open_ || !Check(f_lseek(&file_, offset)) || !Check(f_read(&file_, destination, bytes, &got)))
        return false;
    return got == bytes;
}

bool FatVolume::Write(const void* source, uint32_t bytes) {
    UINT put = 0;
    return open_ && Check(f_write(&file_, source, bytes, &put)) && put == bytes;
}

bool FatVolume::Close() {
    if (!open_)
        return false;
    open_ = false;
    return Check(f_close(&file_));  // flushes data, FAT and directory entry
}

bool FatVolume::Remove(const char* path) {
    if (!mounted_)
        return false;
    const FRESULT r = f_unlink(path);
    if (r == FR_OK || r == FR_NO_FILE || r == FR_NO_PATH)
        return true;
    return Check(r);
}

bool FatVolume::Rename(const char* from, const char* to) { return mounted_ && Check(f_rename(from, to)); }

bool FatVolume::MakeDirectory(const char* path) {
    if (!mounted_)
        return false;
    const FRESULT r = f_mkdir(path);
    return r == FR_OK || r == FR_EXIST || Check(r);
}

bool FatVolume::List(const char* path, Visit visit, void* context) {
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
        // Regular, visible files; names starting with '.' (macOS "._*") are skipped too.
        if (!(info.fattrib & (AM_DIR | AM_HID | AM_SYS)) && info.fname[0] != '.')
            visit(context, info.fname);
    }
    f_closedir(&dir);
    return ok;
}

bool FatVolume::Open(const char* path, BYTE mode) {
    if (!mounted_ || open_)
        return false;
    open_ = Check(f_open(&file_, path, mode));
    return open_;
}

// Low-level errors usually mean the card went away: mount again later.
bool FatVolume::Check(FRESULT r) {
    if (r == FR_DISK_ERR || r == FR_NOT_READY || r == FR_INT_ERR || r == FR_INVALID_OBJECT)
        Unmount();
    return r == FR_OK;
}

void FatVolume::Unmount() {
    if (!mounted_)
        return;
    if (open_)
        f_close(&file_);
    open_ = false;
    ro_sd_unmount();
    mounted_ = false;
    tried_ = false;
}

}  // namespace looper
