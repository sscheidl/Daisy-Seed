#pragma once
#include <vector>
#include <string>
#include <cstring>
using UINT=unsigned;
enum FRESULT {FR_OK,FR_NO_FILE,FR_NO_PATH,FR_DISK_ERR};
constexpr unsigned FA_READ=1;
struct FIL {unsigned slot=0,offset=0;};
struct FATFS {};
namespace fake_sd {
inline std::vector<uint8_t> slots[16];
inline std::string last_path;
inline unsigned calls=0;
inline bool fail=false;
}
inline FRESULT f_mount(FATFS*,const char*,unsigned) {return fake_sd::fail?FR_DISK_ERR:FR_OK;}
inline FRESULT f_open(FIL* f,const char* name,unsigned mode) {
    fake_sd::last_path=name;
    if(mode!=FA_READ || fake_sd::fail) return FR_DISK_ERR;
    f->slot=unsigned(std::stoi(std::string(name).substr(19,2)))-1;f->offset=0;
    return f->slot>=16 || fake_sd::slots[f->slot].empty()?FR_NO_FILE:FR_OK;
}
inline unsigned f_size(const FIL* f) {return unsigned(fake_sd::slots[f->slot].size());}
inline FRESULT f_lseek(FIL* f,unsigned p) {f->offset=p;return fake_sd::fail?FR_DISK_ERR:FR_OK;}
inline FRESULT f_read(FIL* f,void* dst,unsigned n,UINT* got) {
    ++fake_sd::calls;
    if(fake_sd::fail || uint64_t(f->offset)+n>fake_sd::slots[f->slot].size()) return FR_DISK_ERR;
    std::memcpy(dst,fake_sd::slots[f->slot].data()+f->offset,n);*got=n;f->offset+=n;return FR_OK;
}
inline FRESULT f_close(FIL*) {return FR_OK;}
namespace daisy {
struct FatFSInterface {
    struct Config {enum {MEDIA_SD};};
    FATFS fs;
    void Init(unsigned) {}
    FATFS& GetSDFileSystem() {return fs;}
    const char* GetSDPath() {return "0:/";}
};
}
