// Host tests for the WRITABLE build of common/sd (rw/ffconf.h + ro_sd.c + FatFs R0.16)
// on FAT32 and exFAT images built by mkimage: file create/overwrite/rename/delete/mkdir,
// chunked IDMA writes through the bounce buffer, programming-completion waits, CTRL_SYNC,
// every fault path (start failure, error callback, missing completion, programming
// timeout, card removal), full card, and a randomized fault campaign in which files that
// were closed before a fault must survive it.
// Usage: test_rw_sd <fat32.img> <exfat.img>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "ro_sd.h"
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"

#if FF_FS_READONLY
#error "test_rw_sd must be built with -Irw (writable configuration)"
#endif

#include "fake_card.h"

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::abort(); } } while(0)

alignas(32) static uint8_t bounce[4096];
static FATFS fs;
static int Present() { return card::present; }
static void Service() { ++card::services; }
static void Abort() { ++card::aborts; hsd1.State = HAL_SD_STATE_RESET; }

static void Configure(uint32_t bounce_bytes = 1024, uint32_t write_timeout = 50) {
    RoSdConfig c{};
    c.bounce = bounce; c.bounce_bytes = bounce_bytes; c.transfer_timeout_ms = 20;
    c.card_present = Present; c.service = Service; c.abort = Abort; c.write_timeout_ms = write_timeout;
    CHECK(ro_sd_init(&c) == 0);
}
static void Reset(const std::vector<uint8_t>& image) { card::Reset(image); }
static std::vector<uint8_t> Pattern(unsigned seed, size_t size) {
    std::vector<uint8_t> v(size);
    for(size_t i = 0; i < size; ++i) v[i] = uint8_t(i * 131u + seed * 17u + (i >> 9));
    return v;
}
static bool Put(const char* path, const std::vector<uint8_t>& data, unsigned step = 1531) {
    FIL f; UINT w = 0;
    if(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    for(size_t off = 0; off < data.size(); off += step) {
        const UINT n = UINT(std::min<size_t>(step, data.size() - off));
        if(f_write(&f, data.data() + off, n, &w) != FR_OK || w != n) { f_close(&f); return false; }
    }
    return f_close(&f) == FR_OK;
}
static bool Get(const char* path, std::vector<uint8_t>& out) {
    FIL f; UINT got = 0;
    if(f_open(&f, path, FA_READ) != FR_OK) return false;
    out.assign(size_t(f_size(&f)), 0);
    const bool ok = out.empty() || (f_read(&f, out.data(), UINT(out.size()), &got) == FR_OK && got == out.size());
    f_close(&f);
    return ok;
}

static void FileOperations(const std::vector<uint8_t>& image, BYTE type) {
    Reset(image);
    Configure();
    CHECK(ro_sd_mount(&fs) == FR_OK && fs.fs_type == type);
    CHECK(ro_sd_status() == 0);  // writable build: no STA_PROTECT
    CHECK(f_mkdir("0:/looper") == FR_OK && f_mkdir("0:/looper") == FR_EXIST);
    CHECK(f_mkdir("0:/looper/loops") == FR_OK);
    // Sizes around sector, chunk (1 KB bounce) and cluster boundaries, odd write steps.
    std::map<std::string, std::vector<uint8_t>> files;
    const size_t sizes[] = {0, 1, 511, 512, 513, 1024, 4095, 4096, 4097, 70000, 300001};
    unsigned seed = 1;
    for(size_t size : sizes) {
        const std::string path = "0:/looper/f" + std::to_string(size) + ".bin";
        files[path] = Pattern(seed++, size);
        CHECK(Put(path.c_str(), files[path], seed % 2 ? 1531 : 32768));
    }
    CHECK(card::last_src == bounce);  // IDMA only ever sees the bounce buffer
    // Overwrite (create always) with a shorter file, rename, delete, f_sync mid-file.
    files["0:/looper/f300001.bin"] = Pattern(99, 1234);
    CHECK(Put("0:/looper/f300001.bin", files["0:/looper/f300001.bin"]));
    CHECK(f_rename("0:/looper/f1024.bin", "0:/looper/loops/Lange Datei mit Umlaut \x84.bin") == FR_OK);
    files["0:/looper/loops/Lange Datei mit Umlaut \x84.bin"] = files["0:/looper/f1024.bin"];
    files.erase("0:/looper/f1024.bin");
    CHECK(f_unlink("0:/looper/f511.bin") == FR_OK);
    files.erase("0:/looper/f511.bin");
    CHECK(f_unlink("0:/looper/missing.bin") == FR_NO_FILE);
    {
        FIL f; UINT w;
        const auto data = Pattern(7, 5000);
        CHECK(f_open(&f, "0:/looper/sync.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        CHECK(f_write(&f, data.data(), 3000, &w) == FR_OK && f_sync(&f) == FR_OK);
        CHECK(f_write(&f, data.data() + 3000, 2000, &w) == FR_OK && f_close(&f) == FR_OK);
        files["0:/looper/sync.bin"] = data;
    }
    // Remount from scratch: everything is on the card exactly as written.
    ro_sd_unmount();
    CHECK(ro_sd_mount(&fs) == FR_OK);
    for(const auto& [path, data] : files) {
        std::vector<uint8_t> back;
        CHECK(Get(path.c_str(), back) && back == data);
    }
    std::vector<uint8_t> none;
    CHECK(!Get("0:/looper/f1024.bin", none) && !Get("0:/looper/f511.bin", none));
    // Untouched files of the original image are still intact.
    CHECK(Get("0:/NEBULAE/SAMPLES/Drones/10 Air.wav", none) && none.size() == 70000);
    ro_sd_unmount();
}

static void DiskLayer(const std::vector<uint8_t>& image) {
    Reset(image);
    Configure(1024);
    CHECK(ro_sd_mount(&fs) == FR_OK);
    // Chunking through a two-sector bounce buffer; clean before every DMA; service after.
    std::vector<uint8_t> raw = Pattern(5, 5 * 512 + 1), back(5 * 512);
    card::write_counts.clear();
    const unsigned cleans = card::cleans, services = card::services;
    CHECK(disk_write(0, raw.data() + 1, 2000, 5) == RES_OK);
    CHECK((card::write_counts == std::vector<unsigned>{2, 2, 1}));
    CHECK(card::cleans == cleans + 3 && card::services == services + 3);
    CHECK(std::memcmp(card::image.data() + 2000 * 512, raw.data() + 1, 5 * 512) == 0);
    CHECK(disk_read(0, back.data(), 2000, 5) == RES_OK && std::memcmp(back.data(), raw.data() + 1, back.size()) == 0);
    CHECK(disk_write(1, raw.data(), 0, 1) == RES_PARERR && disk_write(0, raw.data(), 0, 0) == RES_PARERR);
    // Programming takes 40 polls: still within the write timeout, returns only afterwards.
    card::busy_after_write = 40;
    const uint32_t t0 = card::tick;
    CHECK(disk_write(0, raw.data(), 2000, 1) == RES_OK && card::busy_polls == 0 && card::tick - t0 >= 40);
    card::busy_after_write = 3;
    CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK);
    card::busy_polls = 10;
    CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK && card::busy_polls == 0);  // sync waits
    // Faults: abort through the hook, STA_NOINIT, nothing more until a fresh mount.
    const card::Mode faults[] = {card::WriteStartFails, card::WriteError, card::WriteNeverComplete,
                                 card::ProgrammingHangs};
    for(card::Mode m : faults) {
        CHECK(ro_sd_mount(&fs) == FR_OK);
        card::mode = m;
        const unsigned aborts = card::aborts;
        const uint32_t start = card::tick;
        CHECK(disk_write(0, raw.data(), 2000, 3) == RES_ERROR);
        CHECK(card::aborts == aborts + 1 && (ro_sd_status() & STA_NOINIT));
        if(m == card::WriteNeverComplete || m == card::ProgrammingHangs) CHECK(card::tick - start >= 50);
        CHECK(disk_write(0, raw.data(), 2000, 1) == RES_NOTRDY);
        card::mode = card::Ok; card::busy_polls = 0;
    }
    CHECK(ro_sd_mount(&fs) == FR_OK);
    card::busy_polls = 1000;
    card::mode = card::ProgrammingHangs;
    CHECK(disk_ioctl(0, CTRL_SYNC, nullptr) == RES_ERROR);  // a sync that never completes fails
    card::mode = card::Ok; card::busy_polls = 0;
    // Removal in the middle of a multi-block transfer.
    CHECK(ro_sd_mount(&fs) == FR_OK);
    card::writes_left = 1;
    CHECK(disk_write(0, raw.data(), 2000, 2) != RES_OK && (ro_sd_status() & STA_NOINIT));
    card::present = true; card::writes_left = -1;
    // Full card: FatFs reports FR_DENIED, no crash, the volume stays usable.
    CHECK(ro_sd_mount(&fs) == FR_OK);
    FIL f; UINT w = 0; FRESULT r = FR_OK;
    std::vector<uint8_t> big(32768, 0x5a);
    CHECK(f_open(&f, "0:/fill.bin", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    for(int i = 0; i < 4096 && r == FR_OK; ++i) {
        r = f_write(&f, big.data(), UINT(big.size()), &w);
        if(r == FR_OK && w < big.size()) break;  // disk full: short write
    }
    CHECK(r == FR_OK && f_close(&f) == FR_OK);
    CHECK(f_unlink("0:/fill.bin") == FR_OK);
    CHECK(Put("0:/after.bin", Pattern(3, 1000)));
    ro_sd_unmount();
}

// Random operations with a card removal at a random block. Every file that was closed
// successfully before the fault, and not touched afterwards, must read back unchanged; the
// file being replaced is old, new or absent (FAT has no transactions, see the Looper's
// recall for how loop.tmp/loop.wav handle that).
static void FaultCampaign(const std::vector<uint8_t>& image, unsigned rounds) {
    std::mt19937 rng(42);
    for(unsigned round = 0; round < rounds; ++round) {
        Reset(image);
        Configure(rng() % 2 ? 512 : 4096);
        CHECK(ro_sd_mount(&fs) == FR_OK);
        f_mkdir("0:/looper");
        std::map<std::string, std::vector<uint8_t>> stable;
        for(int i = 0; i < 4; ++i) {
            const std::string path = "0:/looper/s" + std::to_string(i) + ".bin";
            auto data = Pattern(round * 7 + i, 1 + rng() % 40000);
            CHECK(Put(path.c_str(), data));
            stable[path] = data;
        }
        card::writes_left = long(rng() % 120);
        // The interrupted work: a temp file, then delete+rename (the Looper's save pattern).
        const auto fresh = Pattern(round + 1000, 1 + rng() % 60000);
        bool ok = Put("0:/looper/t.tmp", fresh, 1 + rng() % 9000);
        // From here s0 is being replaced: an interrupted unlink/rename may or may not have
        // reached the card, so s0 is either the old file, the new file, or absent - never
        // anything else. Files not touched by the operation must stay exactly as they were.
        const std::vector<uint8_t> old = stable["0:/looper/s0.bin"];
        if(ok) stable.erase("0:/looper/s0.bin");
        if(ok) ok = f_unlink("0:/looper/s0.bin") == FR_OK;
        if(ok) ok = f_rename("0:/looper/t.tmp", "0:/looper/s0.bin") == FR_OK;
        // Power back: fresh mount, every stable file intact.
        card::present = true; card::writes_left = -1;
        ro_sd_unmount();
        CHECK(ro_sd_mount(&fs) == FR_OK);
        for(const auto& [path, data] : stable) {
            std::vector<uint8_t> back;
            CHECK(Get(path.c_str(), back) && back == data);
        }
        std::vector<uint8_t> s0;
        if(Get("0:/looper/s0.bin", s0)) CHECK(s0 == old || s0 == fresh);
        DIR dir; FILINFO info;
        CHECK(f_opendir(&dir, "0:/looper") == FR_OK);
        while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) {}
        f_closedir(&dir);
        ro_sd_unmount();
    }
}

int main(int argc, char** argv) {
    if(argc != 3) { std::fprintf(stderr, "usage: test_rw_sd fat32.img exfat.img\n"); return 2; }
    std::ifstream a(argv[1], std::ios::binary), b(argv[2], std::ios::binary);
    const std::vector<uint8_t> fat32((std::istreambuf_iterator<char>(a)), {});
    const std::vector<uint8_t> exfat((std::istreambuf_iterator<char>(b)), {});
    CHECK(!fat32.empty() && !exfat.empty());
    {   // A writer needs a bounded abort: HAL_SD_Abort's own waits are not bounded.
        RoSdConfig c{};
        c.bounce = bounce; c.bounce_bytes = 1024; c.transfer_timeout_ms = 20;
        CHECK(ro_sd_init(&c) == -1);
    }
    FileOperations(fat32, FS_FAT32);
    FileOperations(exfat, FS_EXFAT);
    DiskLayer(fat32);
    DiskLayer(exfat);
    FaultCampaign(fat32, 150);
    FaultCampaign(exfat, 150);
    std::printf("PASS common/sd writable: %u checks; FAT32 and exFAT file operations, chunked IDMA "
                "writes, programming waits, sync, faults, removal, full card, 300 fault rounds\n", checks);
    return 0;
}
