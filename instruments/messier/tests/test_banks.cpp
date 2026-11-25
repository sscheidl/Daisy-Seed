// Sample banks: natural order, filters, 0/1/16/17 banks and slots, legacy names,
// incremental scanning (one directory operation per Step) and error paths.
#include "banks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace neb;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while(0)

struct Entry { std::string name, alt; bool dir = false, hidden = false; };
struct FakeDir final : Directory {
    std::map<std::string, std::vector<Entry>> tree;  // path -> entries in card order
    std::map<std::string, int> fail_after;           // path -> Next() calls before -1
    const std::vector<Entry>* current = nullptr;
    std::string path;
    size_t index = 0;
    int calls = 0, opens = 0, nexts = 0;
    bool open = false;
    bool Open(const char* p) override {
        ++calls; ++opens;
        CHECK(!open);  // the scanner never holds two directories
        auto it = tree.find(p);
        if(it == tree.end()) return false;
        current = &it->second; path = p; index = 0; open = true;
        return true;
    }
    int Next(DirEntry& e) override {
        ++calls; ++nexts;
        CHECK(open);
        auto f = fail_after.find(path);
        if(f != fail_after.end() && int(index) >= f->second) return -1;
        if(index >= current->size()) return 0;
        const Entry& s = (*current)[index++];
        std::snprintf(e.name, sizeof e.name, "%s", s.name.c_str());
        std::snprintf(e.alt, sizeof e.alt, "%s", s.alt.c_str());
        e.directory = s.dir; e.hidden = s.hidden;
        return 1;
    }
    void Close() override { CHECK(open); open = false; }
};

static void Run(BankScanner& s, FakeDir& d, unsigned limit = 100000) {
    unsigned steps = 0;
    while(true) {
        const int before = d.calls;
        const bool more = s.Step(d);
        CHECK(d.calls - before <= 2);  // at most one read (+ Open/Close) per main iteration
        if(!more) break;
        CHECK(++steps < limit);
    }
    CHECK(!d.open);
}
static Entry W(const std::string& n, const std::string& alt = "") { return {n, alt, false, false}; }
static Entry D(const std::string& n, const std::string& alt = "") { return {n, alt, true, false}; }
static std::vector<std::string> Files(const BankScanner& s) {
    std::vector<std::string> v;
    for(unsigned i = 0; i < s.FileTable().Count(); ++i) v.push_back(s.FileTable().Name(i));
    return v;
}
static std::string Path(const BankScanner& s, unsigned slot) {
    char p[kPathBytes];
    return s.Path(slot, p, sizeof p) ? p : "";
}

static void Ordering() {
    CHECK(NaturalLess("2 Low hum.wav", "10 Air.wav"));
    CHECK(NaturalLess("a.wav", "B.wav") && NaturalLess("B.wav", "c.wav"));
    CHECK(NaturalLess("1.wav", "01.wav"));        // equal value: fewer leading zeros first
    CHECK(NaturalLess("01.wav", "001.wav"));
    CHECK(NaturalLess("01a.wav", "1b.wav"));      // later text decides before leading zeros
    CHECK(NaturalLess("A.wav", "a.wav"));         // final byte-wise tie-break
    CHECK(NaturalCompare("x.wav", "x.wav") == 0);
    CHECK(NaturalLess("track9", "track10") && NaturalLess("track10", "track10b"));
    CHECK(NaturalLess("z.wav", "\x81" "a.wav"));  // code page bytes by value
    CHECK(NaturalLess("99999999999999999999999", "100000000000000000000000"));  // no overflow
    CHECK(NaturalLess("ab", "abc") && !NaturalLess("abc", "ab"));
    for(const char* a : {"1", "01", "a", "A", "a1", "a01", "b", "10", "\xe9"})
        for(const char* b : {"1", "01", "a", "A", "a1", "a01", "b", "10", "\xe9"}) {
            CHECK(NaturalCompare(a, b) == -NaturalCompare(b, a));  // antisymmetric, total
            if(std::strcmp(a, b)) CHECK(NaturalCompare(a, b) != 0);
        }
    CHECK(IsWavName("a.wav") && IsWavName("B.WAV") && IsWavName("x.Wav"));
    CHECK(!IsWavName(".wav") && !IsWavName("a.wave") && !IsWavName("notes.txt") && !IsWavName("wav"));
    DirEntry e;
    std::strcpy(e.name, "._a.wav"); CHECK(SkippedEntry(e));
    std::strcpy(e.name, ".hidden.wav"); CHECK(SkippedEntry(e));
    std::strcpy(e.name, "ok.wav"); CHECK(!SkippedEntry(e));
    e.hidden = true; CHECK(SkippedEntry(e));
    NameTable t;
    for(int i = 20; i >= 1; --i) { char n[16]; std::snprintf(n, sizeof n, "%d.wav", i); t.Insert(n, ""); }
    CHECK(t.Count() == 16 && std::string(t.Name(0)) == "1.wav" && std::string(t.Name(15)) == "16.wav");
    CHECK(!t.Insert("17.wav", "") && t.Insert("0.wav", "") && std::string(t.Name(15)) == "15.wav");
}

static void Layouts() {
    BankScanner s;
    FakeDir d;
    // No SAMPLES folder / empty card: zero banks, nothing to load.
    s.Start(); Run(s, d);
    CHECK(s.Discovered() && s.BankCount() == 0 && s.BankMask() == 0 && s.SlotMask() == 0 && Path(s, 1).empty());

    // The specification's example.
    d.tree[kSamplesRoot] = {W("Field recording.wav", "FIELDR~1.WAV"), W("kalimba.WAV"), D("Drones"),
                            D("Voices"), D("Empty"), D("System Volume Information"), W("._kalimba.WAV"),
                            W("readme.txt")};
    d.tree[kSamplesRoot][5].hidden = true;
    d.tree["0:/NEBULAE/SAMPLES/Drones"] = {W("10 Air.wav"), W("notes.txt"), D("alt"), W("2 Low hum.wav")};
    d.tree["0:/NEBULAE/SAMPLES/Drones/alt"] = {W("deep.wav")};
    d.tree["0:/NEBULAE/SAMPLES/Voices"] = {W("v.wav")};
    d.tree["0:/NEBULAE/SAMPLES/Empty"] = {D("sub"), W("x.txt")};
    d.tree["0:/NEBULAE/SAMPLES/System Volume Information"] = {W("trap.wav")};
    s.Start(); Run(s, d);
    CHECK(s.BankCount() == 3 && s.BankMask() == 7 && s.FilesBank() == 1 && s.FilesReady());
    CHECK((Files(s) == std::vector<std::string>{"Field recording.wav", "kalimba.WAV"}) && s.SlotMask() == 3);
    CHECK(Path(s, 1) == "0:/NEBULAE/SAMPLES/Field recording.wav" && Path(s, 3).empty() && Path(s, 0).empty());
    CHECK(s.SelectBank(2)); Run(s, d);
    CHECK(s.FilesBank() == 2 && (Files(s) == std::vector<std::string>{"2 Low hum.wav", "10 Air.wav"}));
    CHECK(Path(s, 2) == "0:/NEBULAE/SAMPLES/Drones/10 Air.wav");
    CHECK(s.SelectBank(3)); Run(s, d);
    CHECK(Path(s, 1) == "0:/NEBULAE/SAMPLES/Voices/v.wav");
    CHECK(!s.SelectBank(3) && !s.SelectBank(4) && !s.SelectBank(0));  // active / missing bank: nothing
    // Name outside the code page: open through the 8.3 alias.
    d.tree["0:/NEBULAE/SAMPLES/Voices"] = {W("Ch?r.wav", "CHR~1.WAV")};
    s.Start(3); Run(s, d);
    CHECK(Path(s, 1) == "0:/NEBULAE/SAMPLES/Voices/CHR~1.WAV");

    // No loose WAVs: the first non-empty folder is bank 1.
    d.tree.clear();
    d.tree[kSamplesRoot] = {D("b"), D("a"), W("x.txt")};
    d.tree["0:/NEBULAE/SAMPLES/a"] = {};
    d.tree["0:/NEBULAE/SAMPLES/b"] = {W("1.wav")};
    s.Start(); Run(s, d);
    CHECK(s.BankCount() == 1 && Path(s, 1) == "0:/NEBULAE/SAMPLES/b/1.wav");

    // Legacy card: 01..16 unchanged; with gaps later files move up.
    d.tree.clear();
    for(int i = 16; i >= 1; --i) { char n[16]; std::snprintf(n, sizeof n, "%02d.wav", i); d.tree[kSamplesRoot].push_back(W(n)); }
    s.Start(); Run(s, d);
    for(unsigned i = 1; i <= 16; ++i) { char n[64]; std::snprintf(n, sizeof n, "0:/NEBULAE/SAMPLES/%02u.wav", i); CHECK(Path(s, i) == n); }
    CHECK(s.SlotMask() == 0xFFFF);
    d.tree[kSamplesRoot] = {W("03.wav"), W("01.wav")};
    s.Start(); Run(s, d);
    CHECK(Path(s, 2) == "0:/NEBULAE/SAMPLES/03.wav" && s.SlotMask() == 3);

    // 1 file; 17 files keep the first 16.
    d.tree[kSamplesRoot] = {W("only.wav")};
    s.Start(); Run(s, d);
    CHECK(s.SlotMask() == 1 && s.BankCount() == 1);
    d.tree[kSamplesRoot].clear();
    for(int i = 17; i >= 1; --i) d.tree[kSamplesRoot].push_back(W("s" + std::to_string(i) + ".wav"));
    s.Start(); Run(s, d);
    CHECK(s.FileTable().Count() == 16 && std::string(s.FileTable().Name(15)) == "s16.wav");

    // 16 and 17 banks; more than 16 candidate folders with empty ones among the first.
    for(unsigned folders : {15u, 16u, 17u, 40u}) {
        d.tree.clear();
        for(unsigned i = folders; i >= 1; --i) {
            const std::string n = "bank " + std::to_string(i);
            d.tree[kSamplesRoot].push_back(D(n));
            if(i % 3 == 0 && folders == 40) d.tree[std::string(kSamplesRoot) + "/" + n] = {};  // empty
            else d.tree[std::string(kSamplesRoot) + "/" + n] = {W("x.wav")};
        }
        s.Start(); Run(s, d);
        unsigned expected = folders == 40 ? 16 : std::min(folders, 16u);
        CHECK(s.BankCount() == expected && s.BankMask() == uint16_t((1u << expected) - 1));
        if(folders == 40) {  // bank 16 is the 16th non-empty folder: 1,2,4,5,...,23
            CHECK(s.SelectBank(16)); Run(s, d);
            CHECK(Path(s, 1) == "0:/NEBULAE/SAMPLES/bank 23/x.wav");
        }
    }
    // Loose WAVs plus 16 folders: bank 1 is loose, at most 15 folders follow.
    d.tree[kSamplesRoot].push_back(W("loose.wav"));
    s.Start(); Run(s, d);
    CHECK(s.BankCount() == 16 && Path(s, 1) == "0:/NEBULAE/SAMPLES/loose.wav");
    CHECK(s.SelectBank(16)); Run(s, d);
    CHECK(Path(s, 1) == "0:/NEBULAE/SAMPLES/bank 22/x.wav");  // 15th non-empty folder
}

static void Requests() {
    BankScanner s;
    FakeDir d;
    d.tree[kSamplesRoot] = {D("a"), D("b"), D("c")};
    for(const char* n : {"a", "b", "c"}) d.tree[std::string(kSamplesRoot) + "/" + n] = {W(std::string(n) + ".wav")};
    // Requests during discovery: latest wins.
    s.Start();
    CHECK(s.Step(d) && s.SelectBank(2) && s.SelectBank(3));
    Run(s, d);
    CHECK(s.FilesBank() == 3 && Path(s, 1) == "0:/NEBULAE/SAMPLES/c/c.wav");
    // Restore a bank that no longer exists: bank 1.
    s.Start(9); Run(s, d);
    CHECK(s.FilesBank() == 1);
    // A new selection during a file scan restarts that scan.
    CHECK(s.SelectBank(2) && s.Step(d) && s.SelectBank(3) && !s.SelectBank(3));
    Run(s, d);
    CHECK(s.FilesBank() == 3 && s.FilesReady());
    // Unreadable bank folder: LED code 4, empty table.
    d.fail_after["0:/NEBULAE/SAMPLES/b"] = 0;
    CHECK(s.SelectBank(2)); Run(s, d);
    CHECK(s.FilesBank() == 2 && s.FilesFailed() && !s.FilesReady() && s.SlotMask() == 0);
    // Root read error: discovery stops with what it has, no hang.
    d.fail_after[kSamplesRoot] = 1;
    s.Start(); Run(s, d);
    CHECK(s.State() == BankScanner::Phase::Error && s.BankCount() == 0);
    // Card removal: Reset clears everything.
    d.fail_after.clear();
    s.Start(); Run(s, d); CHECK(s.BankCount() == 3);
    s.Reset();
    CHECK(s.BankCount() == 0 && s.SlotMask() == 0 && !s.Busy() && Path(s, 1).empty());
    // Very long names fit the path buffer.
    const std::string folder(255, 'f'), file(251, 'w');
    d.tree.clear();
    d.tree[kSamplesRoot] = {D(folder)};
    d.tree[std::string(kSamplesRoot) + "/" + folder] = {W(file + ".wav")};
    s.Start(); Run(s, d);
    CHECK(Path(s, 1) == std::string(kSamplesRoot) + "/" + folder + "/" + file + ".wav");
}

int main() {
    Ordering();
    Layouts();
    Requests();
    std::printf("PASS sample banks: %u checks; natural order, filters, 0/1/16/17 banks and slots, legacy names, incremental scan, errors\n", checks);
}
