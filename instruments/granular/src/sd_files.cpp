#include "sd_files.h"
#include <cstring>

namespace neb {

bool FatDirectory::Open(const char* path) {
    if(open_) Close();
    open_ = f_opendir(&dir_, path) == FR_OK;
    return open_;
}

int FatDirectory::Next(DirEntry& entry) {
    if(!open_) return -1;
    if(f_readdir(&dir_, &info_) != FR_OK) return -1;
    if(!info_.fname[0]) return 0;
    std::memcpy(entry.name, info_.fname, sizeof entry.name);
    entry.name[sizeof entry.name - 1] = 0;
#if FF_USE_LFN
    std::memcpy(entry.alt, info_.altname, sizeof entry.alt);
    entry.alt[sizeof entry.alt - 1] = 0;
#else
    entry.alt[0] = 0;
#endif
    entry.directory = (info_.fattrib & AM_DIR) != 0;
    entry.hidden = (info_.fattrib & (AM_HID | AM_SYS)) != 0;
    return 1;
}

void FatDirectory::Close() {
    if(open_) f_closedir(&dir_);
    open_ = false;
}

FileError FatFile::Open(unsigned slot) {
    Close();
    if(!present_()) return FileError::NoCard;
    if(!mounted_) return FileError::Io;
    char path[kPathBytes];
    if(banks_.Busy() || !banks_.Path(slot, path, sizeof path)) return FileError::Missing;
    const FRESULT r = f_open(&handle_, path, FA_READ);
    if(r == FR_NO_FILE || r == FR_NO_PATH || r == FR_INVALID_NAME) return FileError::Missing;
    if(r != FR_OK) { failed_(); return FileError::Io; }
    bank_ = banks_.FilesBank();
    open_ = true;
    return FileError::None;
}

bool FatFile::Read(uint32_t offset, void* dst, uint32_t bytes) {
    if(!open_ || !present_()) return false;
    UINT got = 0;
    if(f_lseek(&handle_, offset) != FR_OK || f_read(&handle_, dst, bytes, &got) != FR_OK || got != bytes) {
        failed_();
        return false;
    }
    return true;
}

void FatFile::Close() {
    if(open_) f_close(&handle_);
    open_ = false;
}

} // namespace neb
