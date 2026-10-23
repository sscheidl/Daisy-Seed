// Host tests for common/sd: the production disk layer (ro_sd.c) and the vendored,
// read-only FatFs R0.16 on real FAT32/exFAT images built by mkimage.
// Usage: test_ro_sd <fat32.img> <exfat.img>
// Standard headers first: on Windows hosts ff.h defines isinf/isnan macros.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "ro_sd.h"
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"

extern "C" SD_HandleTypeDef hsd1;
SD_HandleTypeDef hsd1;

namespace card {
enum Mode { Ok, ErrorCallback, NeverComplete, StartFails, AlwaysBusy };
std::vector<uint8_t> image;
Mode mode = Ok;
bool present = true;
int remove_after = -1; // card disappears after this many DMA transfers
uint32_t tick = 0;
unsigned dma = 0, inits = 0, deinits = 0, aborts = 0, cleans = 0, invalidates = 0, services = 0;
unsigned irq_priority = 99;
const uint8_t* last_dst = nullptr;
std::vector<unsigned> dma_counts;
}

extern "C" {
uint32_t HAL_GetTick(void) { return ++card::tick; } // time advances on every poll
void HAL_NVIC_SetPriority(int irq, uint32_t p, uint32_t) { if(irq == SDMMC1_IRQn) card::irq_priority = p; }
int HAL_SD_Abort(SD_HandleTypeDef*) { ++card::aborts; return 0; }
int HAL_SD_DeInit(SD_HandleTypeDef* h) { ++card::deinits; h->State = HAL_SD_STATE_RESET; return 0; }
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*, int32_t) { ++card::cleans; }
void SCB_InvalidateDCache_by_Addr(uint32_t*, int32_t) { ++card::invalidates; }
uint8_t BSP_SD_Init(void) { ++card::inits; hsd1.State = HAL_SD_STATE_READY; return MSD_OK; }
uint8_t BSP_SD_GetCardState(void) { return card::mode == card::AlwaysBusy ? SD_TRANSFER_BUSY : SD_TRANSFER_OK; }
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info) { info->LogBlockNbr = uint32_t(card::image.size() / 512); }
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t* data, uint32_t sector, uint32_t count) {
    ++card::dma; card::last_dst = reinterpret_cast<uint8_t*>(data); card::dma_counts.push_back(count);
    if(card::remove_after >= 0 && card::dma > unsigned(card::remove_after)) card::present = false;
    if(card::mode == card::StartFails) return MSD_ERROR;
    if(card::mode == card::NeverComplete) return MSD_OK;
    if(card::mode == card::ErrorCallback || uint64_t(sector + count) * 512 > card::image.size()) {
        BSP_SD_ErrorCallback(); return MSD_OK;
    }
    std::memcpy(data, card::image.data() + uint64_t(sector) * 512, size_t(count) * 512);
    BSP_SD_ReadCpltCallback();
    return MSD_OK;
}
}

static int Present() { return card::present; }
static void Service() { ++card::services; }

alignas(32) static uint8_t bounce[4096];
static FATFS fs;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::abort(); } } while(0)

static uint8_t Pattern(unsigned seed, unsigned i) { return uint8_t(i * 31u + seed * 7u + (i >> 9)); }

static std::vector<uint8_t> Load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

static void Configure(uint32_t bounce_bytes, uint32_t timeout = 20, uint32_t budget = 0) {
    RoSdConfig c{};
    c.bounce = bounce; c.bounce_bytes = bounce_bytes; c.transfer_timeout_ms = timeout;
    c.budget_ms = budget; c.card_present = Present; c.service = Service;
    CHECK(ro_sd_init(&c) == 0);
}

static void Reset(const std::vector<uint8_t>& image) {
    card::image = image; card::mode = card::Ok; card::present = true; card::remove_after = -1;
    card::dma = card::inits = card::deinits = card::aborts = card::cleans = card::invalidates = card::services = 0;
    card::dma_counts.clear(); card::irq_priority = 99;
    hsd1.State = HAL_SD_STATE_RESET;
}

static std::set<std::string> List(const char* path) {
    DIR dir; FILINFO info; std::set<std::string> names;
    CHECK(f_opendir(&dir, path) == FR_OK);
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) names.insert(info.fname);
    CHECK(f_closedir(&dir) == FR_OK);
    return names;
}

static void CheckFile(const char* path, unsigned seed, unsigned size) {
    FIL f; UINT got = 0;
    CHECK(f_open(&f, path, FA_READ) == FR_OK);
    CHECK(f_size(&f) == size);
    std::vector<uint8_t> data(size + 1);
    // Odd chunk sizes and an unaligned destination exercise partial and whole sectors.
    for(unsigned pos = 0, step = 1; pos < size; pos += got, step = step * 3 % 1777 + 1) {
        CHECK(f_read(&f, data.data() + 1 + pos, std::min(step, size - pos), &got) == FR_OK && got);
    }
    for(unsigned i = 0; i < size; ++i) CHECK(data[1 + i] == Pattern(seed, i));
    const unsigned offsets[] = {size / 2, 1, size - 1, 0};
    for(unsigned o : offsets) {
        if(o >= size) continue;
        uint8_t b = 0;
        CHECK(f_lseek(&f, o) == FR_OK && f_read(&f, &b, 1, &got) == FR_OK && got == 1 && b == Pattern(seed, o));
    }
    CHECK(f_close(&f) == FR_OK);
}

static void Volume(const std::vector<uint8_t>& image, BYTE type) {
    Reset(image);
    Configure(512);
    CHECK(ro_sd_mount(&fs) == FR_OK);
    CHECK(fs.fs_type == type);
    CHECK(card::inits == 1 && card::irq_priority == 2);
    CHECK(ro_sd_status() == STA_PROTECT);
    const std::set<std::string> root = {"Field recording.wav", "kalimba.WAV", "Caf\x82.wav",
                                        "._kalimba.WAV", "notes.txt", "Drones", "Empty"};
    CHECK(List("0:/NEBULAE/SAMPLES") == root);
    CHECK(List("0:/NEBULAE/SAMPLES/Drones") == (std::set<std::string>{"2 Low hum.wav", "10 Air.wav", "alt"}));
    CHECK(List("0:/NEBULAE/SAMPLES/Empty").empty());
    // exFAT has no 8.3 names; on FAT32 a long name carries its SFN in altname.
    FILINFO info;
    CHECK(f_stat("0:/NEBULAE/SAMPLES/Field recording.wav", &info) == FR_OK);
    CHECK(type == FS_EXFAT ? info.altname[0] == 0 : std::string(info.altname) == "FIELDR~1.WAV");
    CheckFile("0:/NEBULAE/SAMPLES/Field recording.wav", 1, 3000);
    CheckFile("0:/NEBULAE/SAMPLES/kalimba.wav", 2, 700); // case-insensitive lookup
    CheckFile("0:/NEBULAE/SAMPLES/Caf\x82.wav", 3, 513);
    CheckFile("0:/NEBULAE/SAMPLES/Drones/2 Low hum.wav", 6, 1);
    CheckFile("0:/NEBULAE/SAMPLES/Drones/10 Air.wav", 7, 70000);
    FIL f;
    CHECK(f_open(&f, "0:/NEBULAE/SAMPLES/missing.wav", FA_READ) == FR_NO_FILE);
    // Read-only build: files cannot be created.
    CHECK(f_open(&f, "0:/NEBULAE/SAMPLES/new.wav", FA_READ | FA_WRITE | FA_CREATE_ALWAYS) != FR_OK);
    // Every transfer went through the bounce buffer with cache maintenance and service.
    CHECK(card::last_dst == bounce);
    CHECK(card::cleans == card::dma && card::invalidates == card::dma && card::services == card::dma);
    ro_sd_unmount();
}

static void DiskLayer(const std::vector<uint8_t>& image) {
    RoSdConfig bad{};
    CHECK(ro_sd_init(nullptr) == -1 && ro_sd_init(&bad) == -1);
    bad.bounce = bounce + 1; bad.bounce_bytes = 512; bad.transfer_timeout_ms = 1;
    CHECK(ro_sd_init(&bad) == -1); // unaligned
    bad.bounce = bounce; bad.bounce_bytes = 700;
    CHECK(ro_sd_init(&bad) == -1); // not whole sectors
    bad.bounce_bytes = 512; bad.transfer_timeout_ms = 0;
    CHECK(ro_sd_init(&bad) == -1);
    CHECK(ro_sd_mount(&fs) == FR_NOT_READY); // failed init leaves the driver unconfigured

    Reset(image);
    Configure(1024);
    card::present = false;
    CHECK(ro_sd_mount(&fs) == FR_NOT_READY && card::inits == 0);
    CHECK(ro_sd_status() == (STA_NOINIT | STA_NODISK | STA_PROTECT));
    card::present = true;
    CHECK(ro_sd_mount(&fs) == FR_OK && card::inits == 1 && card::deinits == 0);
    CHECK(ro_sd_mount(&fs) == FR_OK && card::inits == 2 && card::deinits == 1); // fresh interface

    // Chunking: five sectors through a two-sector bounce buffer = three transfers.
    std::vector<uint8_t> raw(5 * 512 + 1);
    card::dma_counts.clear(); const unsigned services = card::services;
    CHECK(disk_read(0, raw.data() + 1, 100, 5) == RES_OK);
    CHECK((card::dma_counts == std::vector<unsigned>{2, 2, 1}) && card::services == services + 3);
    CHECK(std::memcmp(raw.data() + 1, image.data() + 100 * 512, 5 * 512) == 0);
    CHECK(disk_read(1, raw.data(), 0, 1) == RES_PARERR && disk_read(0, raw.data(), 0, 0) == RES_PARERR);
    CHECK(disk_status(1) & STA_NOINIT);
    LBA_t count = 0; WORD size = 0; DWORD block = 0;
    CHECK(disk_ioctl(0, GET_SECTOR_COUNT, &count) == RES_OK && count == image.size() / 512);
    CHECK(disk_ioctl(0, GET_SECTOR_SIZE, &size) == RES_OK && size == 512);
    CHECK(disk_ioctl(0, GET_BLOCK_SIZE, &block) == RES_OK && block == 1);
    CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK && disk_ioctl(0, CTRL_TRIM, nullptr) == RES_PARERR);

    // Failures abort the transfer and require a fresh mount.
    const card::Mode failures[] = {card::ErrorCallback, card::StartFails, card::NeverComplete, card::AlwaysBusy};
    for(card::Mode m : failures) {
        CHECK(ro_sd_mount(&fs) == FR_OK);
        card::mode = m; const unsigned aborts = card::aborts; const uint32_t start = card::tick;
        CHECK(disk_read(0, raw.data(), 0, 1) == RES_ERROR);
        CHECK(card::aborts == aborts + 1 && (ro_sd_status() & STA_NOINIT));
        if(m == card::NeverComplete || m == card::AlwaysBusy) CHECK(card::tick - start >= 20);
        CHECK(disk_read(0, raw.data(), 0, 1) == RES_NOTRDY);
        card::mode = card::Ok;
    }
    CHECK(ro_sd_mount(&fs) == FR_OK);
    FIL f; UINT got;
    CHECK(f_open(&f, "0:/NEBULAE/SAMPLES/Drones/10 Air.wav", FA_READ) == FR_OK);
    card::mode = card::ErrorCallback;
    std::vector<uint8_t> big(70000);
    CHECK(f_read(&f, big.data(), 70000, &got) == FR_DISK_ERR);
    card::mode = card::Ok;

    // Removal between transfers.
    CHECK(ro_sd_mount(&fs) == FR_OK);
    card::remove_after = int(card::dma) + 1;
    CHECK(disk_read(0, raw.data(), 0, 5) == RES_NOTRDY);
    CHECK(ro_sd_status() == (STA_NOINIT | STA_NODISK | STA_PROTECT));
    card::present = true; card::remove_after = -1;

    // Overall budget: a long read fails once it is used up; a restart allows more.
    Configure(512, 20, 200);
    CHECK(ro_sd_mount(&fs) == FR_OK);
    CHECK(f_open(&f, "0:/NEBULAE/SAMPLES/Drones/10 Air.wav", FA_READ) == FR_OK);
    CHECK(f_read(&f, big.data(), 70000, &got) == FR_DISK_ERR);
    Configure(512, 20, 1000000);
    CHECK(ro_sd_mount(&fs) == FR_OK);
    ro_sd_restart_budget();
    CHECK(f_open(&f, "0:/NEBULAE/SAMPLES/Drones/10 Air.wav", FA_READ) == FR_OK);
    CHECK(f_read(&f, big.data(), 70000, &got) == FR_OK && got == 70000);
    ro_sd_unmount();
}

static void Put32(std::vector<uint8_t>& img, size_t at, uint32_t v) {
    for(int i = 0; i < 4; ++i) img[at + i] = uint8_t(v >> (8 * i));
}

// Byte offset of the first partition's boot sector (images carry an MBR like real cards).
static size_t VolumeStart(const std::vector<uint8_t>& img) {
    CHECK(img[510] == 0x55 && img[511] == 0xaa && img[446 + 4] != 0);
    const size_t lba = img[454] | img[455] << 8 | img[456] << 16 | size_t(img[457]) << 24;
    return lba * 512;
}

// Header values rejected by R0.16 patch-2, then random corruption.
static void Malformed(const std::vector<uint8_t>& fat32, const std::vector<uint8_t>& exfat) {
    Configure(512);
    const size_t ex = VolumeStart(exfat), f32 = VolumeStart(fat32);
    CHECK(std::memcmp(&exfat[ex + 3], "EXFAT   ", 8) == 0 && std::memcmp(&fat32[f32 + 82], "FAT32   ", 8) == 0);
    std::vector<uint8_t> img = exfat;
    Put32(img, ex + 92, 100); // exFAT ClusterCount below 256
    Reset(img); CHECK(ro_sd_mount(&fs) == FR_NO_FILESYSTEM);
    img = exfat; img[ex + 109] = 30; // SectorsPerClusterShift outside 1..32768 sectors
    Reset(img); CHECK(ro_sd_mount(&fs) == FR_NO_FILESYSTEM);
    img = fat32; Put32(img, f32 + 36, 0x200000); // FATSz32 too large
    Reset(img); CHECK(ro_sd_mount(&fs) == FR_NO_FILESYSTEM);

    std::mt19937 rng(20261023);
    const std::vector<uint8_t>* bases[] = {&fat32, &exfat};
    for(unsigned round = 0; round < 1500; ++round) {
        img = *bases[round & 1];
        const unsigned hits = 1 + rng() % 24;
        for(unsigned h = 0; h < hits; ++h) {
            // Mostly metadata (boot region, FAT/bitmap, directories), sometimes anywhere.
            const size_t limit = rng() % 4 ? std::min<size_t>(img.size(), 600 * 512) : img.size();
            img[rng() % limit] = uint8_t(rng());
        }
        Reset(img);
        if(ro_sd_mount(&fs) != FR_OK) continue;
        const char* dirs[] = {"0:/", "0:/NEBULAE/SAMPLES", "0:/NEBULAE/SAMPLES/Drones"};
        for(const char* d : dirs) {
            DIR dir; FILINFO info;
            if(f_opendir(&dir, d) != FR_OK) continue;
            for(unsigned n = 0; n < 4096 && f_readdir(&dir, &info) == FR_OK && info.fname[0]; ++n) {
                if(info.fattrib & AM_DIR) continue;
                std::string path = std::string(d) + (d[3] ? "/" : "") + info.fname;
                FIL f; UINT got; static uint8_t buf[8192];
                if(f_open(&f, path.c_str(), FA_READ) != FR_OK) continue;
                for(unsigned k = 0; k < 32 && f_read(&f, buf, sizeof buf, &got) == FR_OK && got; ++k) {}
                f_lseek(&f, f_size(&f) / 3);
                f_read(&f, buf, 1, &got);
                f_close(&f);
            }
            f_closedir(&dir);
        }
        ro_sd_unmount();
    }
}

int main(int argc, char** argv) {
    if(argc != 3) { std::fprintf(stderr, "usage: test_ro_sd fat32.img exfat.img\n"); return 2; }
    const std::vector<uint8_t> fat32 = Load(argv[1]), exfat = Load(argv[2]);
    DiskLayer(fat32);
    Volume(fat32, FS_FAT32);
    Volume(exfat, FS_EXFAT);
    DiskLayer(exfat);
    Malformed(fat32, exfat);
    std::printf("PASS common/sd: %u checks; disk layer, FAT32 and exFAT volumes, patch-2 headers, 1500 corrupted images\n", checks);
    return 0;
}
