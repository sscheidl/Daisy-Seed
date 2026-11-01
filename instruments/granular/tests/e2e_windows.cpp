// End-to-end part 1 ("Windows"): a writable FatFs (host-only build of common/sd's
// R0.16 with tests/mkfs/ffconf.h) mounts the card exclusively through SCSI commands
// over the production MscBot + CardReader of the shared card reader. It creates a
// bank, writes WAVs, renames and deletes files, then ejects like Windows does
// (SYNCHRONIZE CACHE, ALLOW MEDIUM REMOVAL, START STOP UNIT). Ownership must only
// return after that eject. Output: the card image and the expected PCM hashes for
// part 2 (test_e2e_return.cpp), which reads them back through Nebulae's read-only
// stack. This is a simulation, not a substitute for the hardware test.
// Usage: e2e_windows <in.img> <out.img> <expected.txt>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "msc_bot.h"
extern "C" {
#include "ff.h"
#include "diskio.h"
}

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

struct MemoryCard final : diag::CardDevice {
    std::vector<uint8_t> image;
    unsigned writes = 0, syncs = 0;
    bool Open(uint32_t& sectors) override { sectors = uint32_t(image.size() / 512); return true; }
    bool Read(uint32_t lba, uint8_t* b) override { std::memcpy(b, &image[size_t(lba) * 512], 512); return true; }
    bool Write(uint32_t lba, const uint8_t* b) override { ++writes; std::memcpy(&image[size_t(lba) * 512], b, 512); return true; }
    bool Sync() override { ++syncs; return true; }
    void Close() override {}
};
struct Port final : diag::MscPort {
    std::vector<std::vector<uint8_t>> in;
    uint16_t receive = 0;
    bool halt_in = false, halt_out = false;
    void Send(const uint8_t* p, uint16_t n) override { in.emplace_back(p, p + n); }
    void Receive(uint16_t n) override { receive = n; }
    void Halt(bool i) override { (i ? halt_in : halt_out) = true; }
};

static MemoryCard card;
static diag::CardReader reader(card);
static Port port;
static diag::MscBot bot(reader, port);
static uint32_t tag = 1;

// One BOT command; returns the CSW status. `data` is read into or written from.
static uint8_t Scsi(std::array<uint8_t, 16> cdb, unsigned cdb_len, bool in, std::vector<uint8_t>& data) {
    std::array<uint8_t, 31> cbw{};
    cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
    ++tag;
    for(int i = 0; i < 4; ++i) cbw[4 + i] = uint8_t(tag >> (8 * i));
    const uint32_t length = uint32_t(data.size());
    for(int i = 0; i < 4; ++i) cbw[8 + i] = uint8_t(length >> (8 * i));
    cbw[12] = in ? 0x80 : 0; cbw[14] = uint8_t(cdb_len);
    std::memcpy(&cbw[15], cdb.data(), cdb_len);
    port.in.clear();
    bot.Out(cbw.data(), cbw.size());
    if(!in && length) {
        for(uint32_t off = 0; off < length; off += 512) {
            if(port.receive != 512 || !port.in.empty()) break;  // device ended the data stage
            port.receive = 0;
            bot.Out(&data[off], 512);
        }
    }
    std::vector<uint8_t> got;
    for(int guard = 0; guard < 100000; ++guard) {
        if(port.halt_in) { port.halt_in = false; bot.ClearHalt(true); continue; }
        if(port.in.empty()) break;
        std::vector<uint8_t> packet = port.in.front();
        port.in.erase(port.in.begin());
        if(packet.size() == 13 && !std::memcmp(packet.data(), "USBS", 4)) {
            CHECK(std::memcmp(&packet[4], &cbw[4], 4) == 0);  // tag echoed
            bot.InComplete();
            if(in) { got.resize(length); std::memcpy(data.data(), got.data(), std::min(got.size(), data.size())); }
            return packet[12];
        }
        got.insert(got.end(), packet.begin(), packet.end());
        if(in && got.size() <= data.size()) std::memcpy(data.data(), got.data(), got.size());
        bot.InComplete();
    }
    CHECK(false && "no CSW");
    return 0xFF;
}
static std::array<uint8_t, 16> Cdb(std::initializer_list<uint8_t> bytes) {
    std::array<uint8_t, 16> c{};
    size_t i = 0;
    for(uint8_t b : bytes) c[i++] = b;
    return c;
}
static std::array<uint8_t, 16> Rw10(uint8_t op, uint32_t lba, uint16_t n) {
    return Cdb({op, 0, uint8_t(lba >> 24), uint8_t(lba >> 16), uint8_t(lba >> 8), uint8_t(lba), 0,
                uint8_t(n >> 8), uint8_t(n)});
}
static uint32_t capacity = 0;

extern "C" {
DSTATUS disk_status(BYTE d) { return d || !reader.Ready() ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE d) { return disk_status(d); }
DRESULT disk_read(BYTE d, BYTE* b, LBA_t s, UINT n) {
    if(d) return RES_PARERR;
    std::vector<uint8_t> data(size_t(n) * 512);
    if(Scsi(Rw10(0x28, uint32_t(s), uint16_t(n)), 10, true, data) != 0) return RES_ERROR;
    std::memcpy(b, data.data(), data.size());
    return RES_OK;
}
DRESULT disk_write(BYTE d, const BYTE* b, LBA_t s, UINT n) {
    if(d) return RES_PARERR;
    std::vector<uint8_t> data(b, b + size_t(n) * 512);
    return Scsi(Rw10(0x2a, uint32_t(s), uint16_t(n)), 10, false, data) == 0 ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE d, BYTE cmd, void* p) {
    if(d) return RES_PARERR;
    std::vector<uint8_t> none;
    if(cmd == CTRL_SYNC) return Scsi(Cdb({0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 10, false, none) == 0 ? RES_OK : RES_ERROR;
    if(cmd == GET_SECTOR_COUNT) { *(LBA_t*)p = capacity; return RES_OK; }
    if(cmd == GET_SECTOR_SIZE) { *(WORD*)p = 512; return RES_OK; }
    if(cmd == GET_BLOCK_SIZE) { *(DWORD*)p = 1; return RES_OK; }
    return RES_PARERR;
}
}

static uint32_t Fnv(const int16_t* pcm, size_t n) {
    uint32_t h = 2166136261u;
    for(size_t i = 0; i < n; ++i) for(int b = 0; b < 2; ++b) { h ^= (uint16_t(pcm[i]) >> (8 * b)) & 0xFF; h *= 16777619u; }
    return h;
}
static void P16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void P32(std::vector<uint8_t>& b, uint32_t v) { P16(b, uint16_t(v)); P16(b, uint16_t(v >> 16)); }
// PCM16 WAV with a deterministic signal; returns the file and the PCM hash.
static std::vector<uint8_t> Wav(unsigned rate, unsigned ch, unsigned frames, unsigned seed, uint32_t& hash,
                                unsigned bits = 16) {
    std::vector<int16_t> pcm(size_t(frames) * ch);
    for(size_t i = 0; i < pcm.size(); ++i) pcm[i] = int16_t(int32_t(i * 2654435761u + seed * 97u) >> 8);
    hash = Fnv(pcm.data(), pcm.size());
    std::vector<uint8_t> b = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    P32(b, 16); P16(b, 1); P16(b, uint16_t(ch)); P32(b, rate); P32(b, rate * ch * bits / 8);
    P16(b, uint16_t(ch * bits / 8)); P16(b, uint16_t(bits));
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    P32(b, uint32_t(pcm.size() * 2));
    for(int16_t v : pcm) P16(b, uint16_t(v));
    const uint32_t riff = uint32_t(b.size() - 8);
    for(int i = 0; i < 4; ++i) b[4 + i] = uint8_t(riff >> (8 * i));
    return b;
}
static void Put(const char* path, const std::vector<uint8_t>& bytes) {
    FIL f; UINT w = 0;
    CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    // Odd chunks: FatFs issues partial-sector and multi-sector writes.
    for(size_t off = 0; off < bytes.size(); off += 1531) {
        const UINT n = UINT(std::min<size_t>(1531, bytes.size() - off));
        CHECK(f_write(&f, &bytes[off], n, &w) == FR_OK && w == n);
    }
    CHECK(f_close(&f) == FR_OK);
}

int main(int argc, char** argv) {
    CHECK(argc == 4);
    std::ifstream in(argv[1], std::ios::binary);
    card.image.assign(std::istreambuf_iterator<char>(in), {});
    CHECK(card.image.size() >= 64 * 512 && card.image.size() % 512 == 0);

    // Before SHARE the medium is not present for Windows.
    bot.Configure();
    std::vector<uint8_t> none, cap(8), sense(18);
    CHECK(Scsi(Cdb({0x00, 0, 0, 0, 0, 0}), 6, false, none) == 1);
    CHECK(reader.Share(true) && reader.Ready());
    CHECK(Scsi(Cdb({0x00, 0, 0, 0, 0, 0}), 6, false, none) == 1);  // UNIT ATTENTION: medium changed
    CHECK(Scsi(Cdb({0x03, 0, 0, 0, 18, 0}), 6, true, sense) == 0 && sense[2] == 6 && sense[12] == 0x28);
    CHECK(Scsi(Cdb({0x00, 0, 0, 0, 0, 0}), 6, false, none) == 0);
    CHECK(Scsi(Cdb({0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 10, true, cap) == 0);
    capacity = (uint32_t(cap[0]) << 24 | uint32_t(cap[1]) << 16 | uint32_t(cap[2]) << 8 | cap[3]) + 1;
    CHECK(capacity == card.image.size() / 512);
    // Windows asks for the caching page: the device reports write-through (WCE=0).
    std::vector<uint8_t> mode(24);
    CHECK(Scsi(Cdb({0x1a, 0, 0x08, 0, 24, 0}), 6, true, mode) == 0 && (mode[6] & 4) == 0);

    static FATFS fs;
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
    std::FILE* expected = std::fopen(argv[3], "w");
    CHECK(expected);
    struct Item { const char* path; const char* listed; unsigned rate, ch, frames, seed, bits; };
    const Item items[] = {
        {"0:/NEBULAE/SAMPLES/Neue Bank/10 Pad.wav", "Neue Bank/10 Pad.wav", 44100, 2, 9000, 11, 16},
        {"0:/NEBULAE/SAMPLES/Neue Bank/2 Kick.WAV", "Neue Bank/2 Kick.WAV", 48000, 1, 4801, 12, 16},
        {"0:/NEBULAE/SAMPLES/Neue Bank/Field 24bit.wav", "Neue Bank/Field 24bit.wav", 48000, 2, 100, 13, 24},
        {"0:/NEBULAE/SAMPLES/A loose take.wav", "A loose take.wav", 48000, 2, 12345, 14, 16},
    };
    CHECK(f_mkdir("0:/NEBULAE/SAMPLES/Neue Bank") == FR_OK);
    for(const Item& it : items) {
        uint32_t hash = 0;
        Put(it.path, Wav(it.rate, it.ch, it.frames, it.seed, hash, it.bits));
        std::fprintf(expected, "%s|%s|%u|%u|%u|%08x\n", it.listed, it.bits == 16 ? "OK" : "FORMAT",
                     it.rate, it.ch, it.frames, hash);
    }
    // Rename and delete through Windows.
    CHECK(f_rename("0:/NEBULAE/SAMPLES/kalimba.WAV", "0:/NEBULAE/SAMPLES/Kalimba renamed.wav") == FR_OK);
    CHECK(f_unlink("0:/NEBULAE/SAMPLES/Field recording.wav") == FR_OK);
    std::fprintf(expected, "ABSENT|kalimba.WAV\nABSENT|Field recording.wav\nPRESENT|Kalimba renamed.wav\n");
    std::fclose(expected);
    CHECK(card.writes > 0);

    // Ownership cannot be taken back while Windows owns the card.
    CHECK(!reader.Return() && reader.Ready());
    CHECK(f_mount(nullptr, "0:", 0) == FR_OK);  // Windows flushes its cache
    // Windows "Safely remove": prevent/allow, sync, then START STOP UNIT (LoEj).
    CHECK(Scsi(Cdb({0x1e, 0, 0, 0, 1, 0}), 6, false, none) == 0);
    CHECK(Scsi(Cdb({0x1b, 0, 0, 0, 2, 0}), 6, false, none) == 1);  // refused while prevented
    CHECK(reader.State() == diag::ReaderState::Shared);
    CHECK(Scsi(Cdb({0x1e, 0, 0, 0, 0, 0}), 6, false, none) == 0);
    const unsigned syncs = card.syncs;
    CHECK(Scsi(Cdb({0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 10, false, none) == 0);
    CHECK(Scsi(Cdb({0x1b, 0, 0, 0, 2, 0}), 6, false, none) == 0);
    CHECK(reader.State() == diag::ReaderState::Ejected && card.syncs >= syncs + 2);
    CHECK(Scsi(Cdb({0x00, 0, 0, 0, 0, 0}), 6, false, none) == 1);  // gone for Windows
    CHECK(reader.Return() && reader.Local());

    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(card.image.data()), std::streamsize(card.image.size()));
    CHECK(out.good());
    std::printf("PASS e2e Windows side: %u checks, %u blocks written over MSC, eject before return\n", checks, card.writes);
    return 0;
}
