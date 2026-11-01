// End-to-end part 2 ("return to Nebulae"): the card image written by e2e_windows
// through MSC is mounted again by the production read-only stack (common/sd ro_sd.c,
// FatFs R0.16 read-only, banks.cpp, sd_files.cpp, the WAV Loader). Banks are
// rescanned, every slot is loaded, and the PCM hashes must equal what "Windows"
// wrote. Renamed/deleted files must show up as such.
// Usage: test_e2e_return <card.img> <expected.txt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "ro_sd.h"
#include "stm32h7xx_hal.h"
#include "util/bsp_sd_diskio.h"
#include "sd_files.h"

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

extern "C" SD_HandleTypeDef hsd1;
SD_HandleTypeDef hsd1;
static std::vector<uint8_t> image;
static uint32_t tick = 0;
static unsigned dma = 0;
extern "C" {
uint32_t HAL_GetTick(void) { return ++tick; }
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t) {}
int HAL_SD_Abort(SD_HandleTypeDef*) { return 0; }
int HAL_SD_DeInit(SD_HandleTypeDef* h) { h->State = HAL_SD_STATE_RESET; return 0; }
void SCB_CleanInvalidateDCache_by_Addr(uint32_t*, int32_t) {}
void SCB_InvalidateDCache_by_Addr(uint32_t*, int32_t) {}
uint8_t BSP_SD_Init(void) { hsd1.State = HAL_SD_STATE_READY; return MSD_OK; }
uint8_t BSP_SD_GetCardState(void) { return SD_TRANSFER_OK; }
void BSP_SD_GetCardInfo(DSY_SD_CardInfoTypeDef* info) { info->LogBlockNbr = uint32_t(image.size() / 512); }
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t* data, uint32_t sector, uint32_t count) {
    ++dma;
    if(uint64_t(sector + count) * 512 > image.size()) { BSP_SD_ErrorCallback(); return MSD_OK; }
    std::memcpy(data, image.data() + uint64_t(sector) * 512, size_t(count) * 512);
    BSP_SD_ReadCpltCallback();
    return MSD_OK;
}
}

static bool Present() { return true; }
static bool io_failed = false;
static void Failed() { io_failed = true; }
alignas(32) static uint8_t bounce[512];
static uint32_t Fnv(const int16_t* pcm, size_t n) {
    uint32_t h = 2166136261u;
    for(size_t i = 0; i < n; ++i) for(int b = 0; b < 2; ++b) { h ^= (uint16_t(pcm[i]) >> (8 * b)) & 0xFF; h *= 16777619u; }
    return h;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    std::ifstream in(argv[1], std::ios::binary);
    image.assign(std::istreambuf_iterator<char>(in), {});
    CHECK(!image.empty());
    struct Expect { std::string status; unsigned rate, ch, frames; uint32_t hash; };
    std::map<std::string, Expect> expected;
    std::set<std::string> absent, present;
    std::ifstream ex(argv[2]);
    for(std::string line; std::getline(ex, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for(std::string part; std::getline(ss, part, '|');) f.push_back(part);
        if(f[0] == "ABSENT") absent.insert(f[1]);
        else if(f[0] == "PRESENT") present.insert(f[1]);
        else expected[f[0]] = {f[1], unsigned(std::stoul(f[2])), unsigned(std::stoul(f[3])),
                               unsigned(std::stoul(f[4])), uint32_t(std::stoul(f[5], nullptr, 16))};
    }
    CHECK(expected.size() == 4 && absent.size() == 2 && present.size() == 1);

    RoSdConfig c{};
    c.bounce = bounce; c.bounce_bytes = sizeof bounce; c.transfer_timeout_ms = 20;
    CHECK(ro_sd_init(&c) == 0);
    static FATFS fs;
    CHECK(ro_sd_mount(&fs) == FR_OK);
    static neb::BankScanner banks;
    static neb::FatDirectory dir;
    static neb::FatFile file(banks, Present, Failed);
    file.SetMounted(true);
    static std::vector<int16_t> pcm(1u << 20);
    static neb::FileBridge bridge;
    static neb::Loader loader;
    loader.Init(file, pcm.data(), pcm.size() * 2, bridge);

    banks.Start();
    while(banks.Step(dir)) {}
    // Banks: loose WAVs, "Drones", "Neue Bank" ("Empty" has no WAV and is not a bank).
    CHECK(banks.BankCount() == 3 && banks.BankMask() == 7);
    std::set<std::string> seen;
    unsigned matched = 0;
    for(unsigned bank = 1; bank <= banks.BankCount(); ++bank) {
        if(bank != banks.FilesBank()) { CHECK(banks.SelectBank(bank)); while(banks.Step(dir)) {} }
        CHECK(banks.FilesReady() && banks.FilesBank() == bank);
        for(unsigned slot = 1; slot <= banks.FileTable().Count(); ++slot) {
            char path[neb::kPathBytes];
            CHECK(banks.Path(slot, path, sizeof path));
            const std::string listed = std::string(path).substr(std::strlen(neb::kSamplesRoot) + 1);
            seen.insert(listed);
            const auto it = expected.find(listed);
            if(it == expected.end()) continue;
            CHECK(loader.Request(slot));
            for(unsigned guard = 0; loader.state != neb::Loader::State::Idle; ++guard) {
                CHECK(guard < 100000);
                loader.Service();
                // Audio side of the gate/ack handshake.
                bridge.ack.store(bridge.gate.load(), std::memory_order_release);
            }
            const Expect& e = it->second;
            if(e.status == "OK") {
                CHECK(loader.error == neb::FileError::None && loader.active == slot);
                CHECK(bridge.published.rate == e.rate && bridge.published.channels == e.ch
                      && bridge.published.frames == e.frames);
                CHECK(Fnv(pcm.data(), size_t(e.frames) * e.ch) == e.hash);
                CHECK(file.Bank() == bank);
            } else {
                CHECK(loader.error == neb::FileError::Format);  // 24-bit: slot occupied, load refused
            }
            ++matched;
        }
    }
    CHECK(matched == expected.size());
    for(const std::string& name : absent) CHECK(!seen.count(name));
    for(const std::string& name : present) CHECK(seen.count(name));
    // Natural order inside the new bank: 2 before 10, then the 24-bit file.
    CHECK(banks.SelectBank(3) || banks.FilesBank() == 3);
    while(banks.Step(dir)) {}
    CHECK(std::string(banks.FileTable().Name(0)) == "2 Kick.WAV" && std::string(banks.FileTable().Name(1)) == "10 Pad.wav");
    CHECK(!io_failed && dma > 0);
    ro_sd_unmount();
    std::printf("PASS e2e return: %u checks; Windows writes re-read after eject/return/rescan, %u files hash-verified\n",
                checks, matched);
    return 0;
}
