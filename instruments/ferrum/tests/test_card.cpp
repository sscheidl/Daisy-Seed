// SPDX-License-Identifier: MIT
// The Looper's card stack as on the board, on FAT32 and exFAT images: LoopStore + FatVolume +
// common/sd (writable FatFs R0.16, ro_sd.c) over a fake SDMMC card. Save and recall,
// import of long and 44.1 kHz names, and a power-loss campaign: the card disappears after
// every possible block of a save (loop.tmp data, close, delete, rename); after the
// "power cycle" the recall must give exactly the old or the new loop, and the card must
// stay usable for the next save.
// Usage: test_card <fat32.img> <exfat.img> <exfat-32k.img>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
#include "fat_volume.h"
#include "fake_card.h"
#include "loop_store.h"
#include "looper_engine.h"
#include "ro_sd.h"
#include "wav.h"

using namespace looper;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

alignas(32) static uint8_t bounce[32768];
static uint32_t clockMs = 0;
static bool Present() { return card::present; }
static uint32_t Now() { return clockMs; }
static void Abort() { ++card::aborts; hsd1.State = HAL_SD_STATE_RESET; }
static void InitDriver() {
    RoSdConfig c{};
    c.bounce = bounce; c.bounce_bytes = sizeof bounce; c.transfer_timeout_ms = 250;
    c.write_timeout_ms = 1000; c.card_present = [] { return card::present ? 1 : 0; }; c.abort = Abort;
    CHECK(ro_sd_init(&c) == 0);
}

struct View : EngineView {
    Engine& e;
    explicit View(Engine& engine) : e(engine) {}
    EngineSnapshot Get() override { return {e.ContentVersion(), !e.Busy(), e.ActiveFrames(), e.Length(), e.TailLength()}; }
};
struct Port : ImportPort {
    Engine& e;
    Reply reply = Reply::Idle;
    Frame* buffer = nullptr;
    explicit Port(Engine& engine) : e(engine) {}
    void Begin(uint32_t frames) override { buffer = e.BeginImport(frames); reply = buffer ? Reply::Granted : Reply::Denied; }
    void Commit(uint32_t length, uint32_t tail) override { reply = e.CommitImport(length, tail) ? Reply::Done : Reply::Failed; }
    void Cancel() override { e.CancelImport(); reply = Reply::Idle; }
    Reply Poll(Frame*& out) override { out = buffer; return reply; }
};
// One power-on: engine, volume and store from scratch on the current card image.
struct Board {
    static constexpr uint32_t kFrames = 48000;
    std::vector<Frame> a = std::vector<Frame>(kFrames), b = std::vector<Frame>(kFrames);
    std::vector<uint8_t> transfer = std::vector<uint8_t>(32768);
    std::vector<float> table = std::vector<float>(resample::kTableSize);
    Engine engine;
    FatVolume volume;
    LoopStore store;
    View view{engine};
    Port port{engine};
    uint64_t t = 0;
    Board() {
        InitDriver();
        volume.Init(Present, Now);
        CHECK(engine.Init(48000.f, a.data(), b.data(), kFrames));
        CHECK(store.Init(&volume, transfer.data(), uint32_t(transfer.size()), kFrames, "0:/looper"));
        CHECK(store.EnableResampling(table.data(), uint32_t(table.size())));
    }
    // Power off: RAM is gone, including FatFs' registration of this volume (no writes).
    ~Board() { ro_sd_unmount(); }
    void Run(uint32_t ms, float level = 0.f, double hz = 220.0) {
        float in[48], out[48];
        for (uint32_t m = 0; m < ms; ++m) {
            for (int i = 0; i < 48; ++i, ++t)
                in[i] = level * float(std::sin(2.0 * 3.14159265358979 * hz * double(t) / 48000.0));
            engine.Process(in, in, out, out, 48);
            store.Service(view, port, clockMs++);
        }
    }
    void Record(uint32_t ms, float level, double hz) {
        engine.PressRecord();
        Run(ms, level, hz);
        engine.PressRecord();
    }
    bool SaveDone(uint32_t saves, uint32_t limit = 20000) {
        for (uint32_t m = 0; m < limit && store.Saves() < saves; ++m)
            Run(1);
        return store.Saves() >= saves;
    }
};
static std::vector<Frame> Snapshot(const Engine& e) {
    std::vector<Frame> s(e.Length() + e.TailLength());
    for (uint32_t i = 0; i < s.size(); ++i)
        s[i] = e.StoredFrame(i);
    return s;
}
static bool Same(const std::vector<Frame>& x, const std::vector<Frame>& y) {
    return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(Frame)) == 0);
}

static void SaveRecallImport(const std::vector<uint8_t>& image, BYTE type, unsigned expectBurst = 0) {
    card::Reset(image);
    card::write_counts.clear();
    std::vector<Frame> take;
    {
        auto board = std::make_unique<Board>();
        CHECK(!board->store.Recall(board->engine));  // empty card folder: nothing to recall
        CHECK(board->volume.Mounted());
        board->Record(300, .5f, 220.0);
        CHECK(board->SaveDone(1));
        take = Snapshot(board->engine);
        CHECK(card::last_src == bounce && card::dma_writes > 0);
        // 32 KB LoopStore chunks become multi-block transfers: FatFs writes whole clusters
        // at once, up to the 64-sector bounce buffer.
        unsigned maximum = 0;
        for (unsigned n : card::write_counts) maximum = std::max(maximum, n);
        FATFS* fs = nullptr;
        DWORD free = 0;
        CHECK(f_getfree("0:", &free, &fs) == FR_OK);
        // The loop data is offset by the WAV header, so one sector per chunk boundary goes
        // through FatFs' sector window: a 32 KB cluster gives 63-sector transfers.
        CHECK(maximum + 1 >= std::min<unsigned>(64, fs->csize) && maximum <= 64);
        if (expectBurst) CHECK(maximum == expectBurst);
    }
    {   // Power cycle: the same loop comes back, on this file system.
        auto board = std::make_unique<Board>();
        CHECK(board->store.Recall(board->engine));
        CHECK(Same(Snapshot(board->engine), take));
        FATFS* fs = nullptr;
        DWORD free = 0;
        CHECK(f_getfree("0:", &free, &fs) == FR_OK && fs->fs_type == type);
    }
    {   // Import: a long, non-ASCII name and a 44.1 kHz file in looper/loops/.
        auto board = std::make_unique<Board>();
        CHECK(board->store.Recall(board->engine));
        FIL f; UINT w;
        std::vector<uint8_t> bytes(44 + 4410 * 4);
        auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(v >> (8 * i)); };
        std::memcpy(bytes.data(), "RIFF", 4); put32(4, uint32_t(bytes.size() - 8)); std::memcpy(&bytes[8], "WAVEfmt ", 8);
        put32(16, 16); bytes[20] = 1; bytes[22] = 2; put32(24, 44100); put32(28, 44100 * 4); bytes[32] = 4; bytes[34] = 16;
        std::memcpy(&bytes[36], "data", 4); put32(40, 4410 * 4);
        for (size_t i = 44; i < bytes.size(); i += 2) { bytes[i] = uint8_t(i); bytes[i + 1] = 0x10; }
        const char* name = "0:/looper/loops/A sehr langer Name mit Umlaut \x84 und 44,1 kHz.wav";
        CHECK(f_open(&f, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        CHECK(f_write(&f, bytes.data(), UINT(bytes.size()), &w) == FR_OK && f_close(&f) == FR_OK);
        CHECK(f_open(&f, "0:/looper/loops/._A resource fork.wav", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK && f_close(&f) == FR_OK);
        board->store.RequestImport();
        board->Run(2000);
        CHECK(board->engine.Length() == 4800);  // 0.1 s at 44.1 kHz converted to 48 kHz
        CHECK(std::string(board->store.LastImportName()).find("sehr langer Name") != std::string::npos);
        CHECK(board->store.LastFault() == LoopStore::Fault::None);
    }
    std::printf("PASS card on %s: save as multi-sector transfers (whole clusters), recall after power-up, import with long names and 44.1 kHz\n",
                type == FS_EXFAT ? "exFAT" : "FAT32");
}

static void PowerLoss(const std::vector<uint8_t>& image, BYTE type) {
    // Prepare: loop A saved on the card.
    card::Reset(image);
    std::vector<Frame> loopA, loopB;
    std::vector<uint8_t> withA;
    {
        auto board = std::make_unique<Board>();
        board->store.Recall(board->engine);
        board->Record(250, .5f, 220.0);
        CHECK(board->SaveDone(1));
        loopA = Snapshot(board->engine);
        withA = card::image;
    }
    // How many block writes does the replacing save take in full?
    unsigned total = 0;
    {
        card::Reset(withA);
        auto board = std::make_unique<Board>();
        CHECK(board->store.Recall(board->engine));
        board->engine.Erase();
        board->Run(10);
        board->Record(150, .4f, 330.0);
        const unsigned before = card::blocks_written;
        CHECK(board->SaveDone(1));
        total = card::blocks_written - before;
        loopB = Snapshot(board->engine);
        CHECK(!Same(loopA, loopB));
    }
    CHECK(total > 100);
    unsigned oldSeen = 0, newSeen = 0;
    for (unsigned cut = 0; cut <= total + 2; ++cut) {
        card::Reset(withA);
        {
            auto board = std::make_unique<Board>();
            CHECK(board->store.Recall(board->engine));
            board->engine.Erase();
            board->Run(10);
            board->Record(150, .4f, 330.0);
            card::writes_left = long(cut);  // power fails after `cut` more block writes
            board->Run(4000);
        }
        card::present = true;
        card::writes_left = -1;
        // Power-up after the loss: exactly loop A or loop B, nothing else.
        auto board = std::make_unique<Board>();
        const bool recalled = board->store.Recall(board->engine);
        CHECK(recalled);
        const auto got = Snapshot(board->engine);
        const bool isA = Same(got, loopA), isB = Same(got, loopB);
        if (!(isA || isB)) std::fprintf(stderr, "cut %u of %u: recalled %zu frames, neither loop\n", cut, total, got.size());
        CHECK(isA || isB);
        oldSeen += isA;
        newSeen += isB;
        // The card stays consistent: the next save and recall work.
        board->engine.Erase();
        board->Run(10);
        board->Record(100, .3f, 440.0);
        CHECK(board->SaveDone(1));
        const auto loopC = Snapshot(board->engine);
        auto again = std::make_unique<Board>();
        CHECK(again->store.Recall(again->engine) && Same(Snapshot(again->engine), loopC));
    }
    CHECK(oldSeen > 0 && newSeen > 0);
    std::printf("PASS power loss on %s: %u cut points over %u block writes; old loop %u times, new loop %u times, "
                "never anything else; next save works every time\n",
                type == FS_EXFAT ? "exFAT" : "FAT32", total + 3, total, oldSeen, newSeen);
}

int main(int argc, char** argv) {
    CHECK(argc == 4);
    std::ifstream a(argv[1], std::ios::binary), b(argv[2], std::ios::binary), c(argv[3], std::ios::binary);
    const std::vector<uint8_t> fat32((std::istreambuf_iterator<char>(a)), {});
    const std::vector<uint8_t> exfat((std::istreambuf_iterator<char>(b)), {});
    const std::vector<uint8_t> exfat32k((std::istreambuf_iterator<char>(c)), {});
    CHECK(!fat32.empty() && !exfat.empty() && !exfat32k.empty());
    SaveRecallImport(fat32, FS_FAT32);
    SaveRecallImport(exfat, FS_EXFAT);
    SaveRecallImport(exfat32k, FS_EXFAT, 63);  // 32 KB clusters: 63-sector transfers
    PowerLoss(fat32, FS_FAT32);
    PowerLoss(exfat, FS_EXFAT);
    std::printf("PASS looper card stack: %u checks\n", checks);
    return 0;
}
