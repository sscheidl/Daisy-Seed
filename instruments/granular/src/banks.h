// Sample banks and free file names (docs/SAMPLE_BANKS.md). No FatFs dependency: the
// card is read through the small Directory interface, one entry per Step(), so the
// main loop keeps servicing USB MIDI and LEDs. Host-tested in tests/test_banks.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

namespace neb {

constexpr unsigned kMaxBanks = 16, kMaxSlots = 16;
constexpr size_t kNameBytes = 256;  // FF_MAX_LFN 255 + terminator
constexpr size_t kAltBytes = 13;    // 8.3 alias + terminator (empty on exFAT)
constexpr size_t kPathBytes = 19 + kNameBytes + kNameBytes + 1;  // "0:/NEBULAE/SAMPLES/" b "/" f
constexpr const char* kSamplesRoot = "0:/NEBULAE/SAMPLES";

struct DirEntry {
    char name[kNameBytes] = {};
    char alt[kAltBytes] = {};
    bool directory = false;
    bool hidden = false;  // FAT hidden or system attribute
};

class Directory {
  public:
    virtual bool Open(const char* path) = 0;
    // 1 = entry stored, 0 = end of directory, -1 = read error.
    virtual int Next(DirEntry& entry) = 0;
    virtual void Close() = 0;

  protected:
    ~Directory() = default;
};

// Deterministic natural order: ASCII case-insensitive, digit runs by numeric value
// (leading zeros: shorter first, as a late tie-break), other bytes by value, then a
// final byte-wise comparison. Returns <0, 0 or >0.
int NaturalCompare(const char* a, const char* b);
inline bool NaturalLess(const char* a, const char* b) { return NaturalCompare(a, b) < 0; }
bool IsWavName(const char* name);
bool SkippedEntry(const DirEntry& entry);  // '.', '._*', hidden, system

// The kMaxSlots smallest names in natural order; larger ones are dropped.
class NameTable {
  public:
    void Clear() { count_ = 0; }
    bool Insert(const char* name, const char* alt);  // false if dropped
    unsigned Count() const { return count_; }
    const char* Name(unsigned i) const { return name_[i]; }
    const char* Alt(unsigned i) const { return alt_[i]; }

  private:
    char name_[kMaxSlots][kNameBytes] = {};
    char alt_[kMaxSlots][kAltBytes] = {};
    unsigned count_ = 0;
};

class BankScanner {
  public:
    enum class Phase : uint8_t { Idle, Root, Probe, Files, Error };
    // Discards everything and starts discovering banks (after a successful mount).
    // `bank` is the bank whose file list is read afterwards (clamped to what exists).
    void Start(unsigned bank = 1);
    // Card removed or unmounted: clears all tables. No directory call follows.
    void Reset();
    // Reads the file list of another bank; ignored for a missing or the shown bank.
    // While banks are still being discovered the latest request wins.
    bool SelectBank(unsigned bank);
    // One directory operation. Returns true while work remains.
    bool Step(Directory& dir);
    bool Busy() const { return phase_ == Phase::Root || phase_ == Phase::Probe || phase_ == Phase::Files; }
    Phase State() const { return phase_; }

    // Results. Banks are numbered 1..BankCount(); bank 1 holds the loose WAVs if any.
    unsigned BankCount() const { return (loose_ ? 1u : 0u) + folders_.Count(); }
    uint16_t BankMask() const;
    bool Discovered() const { return discovered_; }
    // Bank whose file list FileTable() holds (0 = none yet), and whether reading it failed.
    unsigned FilesBank() const { return files_bank_; }
    bool FilesReady() const { return files_ready_; }
    bool FilesFailed() const { return files_failed_; }
    unsigned ScanningBank() const { return phase_ == Phase::Files ? target_ : 0; }
    uint16_t SlotMask() const;  // occupied slots of FilesBank()
    const NameTable& FileTable() const { return files_; }
    // Full path of a slot of FilesBank(); false if the slot is empty. Uses the 8.3
    // alias when a long name contains characters outside the code page ('?').
    bool Path(unsigned slot, char* out, size_t cap) const;

  private:
    bool FolderPath(unsigned bank, char* out, size_t cap) const;
    void BeginFiles();
    void Fail();
    Phase phase_ = Phase::Idle;
    NameTable folders_, candidates_, files_;
    DirEntry entry_;
    char bound_[kNameBytes] = {};
    bool bounded_ = false, loose_ = false, open_ = false, discovered_ = false;
    bool probe_more_ = false, files_ready_ = false, files_failed_ = false;
    unsigned probe_ = 0, target_ = 0, files_bank_ = 0, pending_ = 0;
};

} // namespace neb
