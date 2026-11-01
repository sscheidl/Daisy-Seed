#include "banks.h"
#include <cstring>

namespace neb {
namespace {
unsigned char Fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
bool Digit(unsigned char c) { return c >= '0' && c <= '9'; }
void Copy(char* dst, const char* src, size_t cap) {
    size_t i = 0;
    for(; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = 0;
}
// Appends; false if the result would not fit.
bool Append(char* out, size_t cap, size_t& used, const char* text) {
    for(; *text; ++text) {
        if(used + 1 >= cap) return false;
        out[used++] = *text;
    }
    out[used] = 0;
    return true;
}
const char* OpenName(const char* name, const char* alt) {
    return std::strchr(name, '?') && alt[0] ? alt : name;
}
} // namespace

int NaturalCompare(const char* a, const char* b) {
    const unsigned char* x = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* y = reinterpret_cast<const unsigned char*>(b);
    int tie = 0;  // first difference in leading zeros, decided only if all else is equal
    while(*x && *y) {
        if(Digit(*x) && Digit(*y)) {
            const unsigned char* xs = x; const unsigned char* ys = y;
            while(*x == '0') ++x;
            while(*y == '0') ++y;
            const unsigned char* xv = x; const unsigned char* yv = y;
            while(Digit(*x)) ++x;
            while(Digit(*y)) ++y;
            const ptrdiff_t xl = x - xv, yl = y - yv;
            if(xl != yl) return xl < yl ? -1 : 1;
            for(ptrdiff_t i = 0; i < xl; ++i)
                if(xv[i] != yv[i]) return xv[i] < yv[i] ? -1 : 1;
            const ptrdiff_t xz = xv - xs, yz = yv - ys;
            if(!tie && xz != yz) tie = xz < yz ? -1 : 1;
            continue;
        }
        const unsigned char cx = Fold(*x), cy = Fold(*y);
        if(cx != cy) return cx < cy ? -1 : 1;
        ++x; ++y;
    }
    if(*x || *y) return *x ? 1 : -1;
    if(tie) return tie;
    const int raw = std::strcmp(a, b);  // byte-wise, deterministic for case variants
    return raw < 0 ? -1 : raw > 0 ? 1 : 0;
}

bool IsWavName(const char* name) {
    const size_t n = std::strlen(name);
    return n > 4 && name[n - 4] == '.' && Fold(name[n - 3]) == 'w' && Fold(name[n - 2]) == 'a'
           && Fold(name[n - 1]) == 'v';
}

bool SkippedEntry(const DirEntry& e) { return !e.name[0] || e.name[0] == '.' || e.hidden; }

bool NameTable::Insert(const char* name, const char* alt) {
    unsigned at = count_;
    while(at > 0 && NaturalLess(name, name_[at - 1])) --at;
    if(at >= kMaxSlots) return false;
    const unsigned last = count_ < kMaxSlots ? count_ : kMaxSlots - 1;
    for(unsigned i = last; i > at; --i) {
        std::memcpy(name_[i], name_[i - 1], kNameBytes);
        std::memcpy(alt_[i], alt_[i - 1], kAltBytes);
    }
    Copy(name_[at], name, kNameBytes);
    Copy(alt_[at], alt, kAltBytes);
    if(count_ < kMaxSlots) ++count_;
    return true;
}

void BankScanner::Reset() {
    phase_ = Phase::Idle;
    folders_.Clear(); candidates_.Clear(); files_.Clear();
    bounded_ = loose_ = open_ = discovered_ = probe_more_ = false;
    files_ready_ = files_failed_ = false;
    probe_ = target_ = files_bank_ = pending_ = 0;
}

void BankScanner::Start(unsigned bank) {
    Reset();
    target_ = bank ? bank : 1;
    phase_ = Phase::Root;
}

bool BankScanner::SelectBank(unsigned bank) {
    if(!discovered_ && (phase_ == Phase::Root || phase_ == Phase::Probe)) {
        if(bank < 1 || bank > kMaxBanks) return false;
        target_ = bank;  // latest wins, served after discovery
        return true;
    }
    if(bank < 1 || bank > BankCount()) return false;
    if(phase_ == Phase::Files) {
        if(bank == (pending_ ? pending_ : target_)) return false;
        pending_ = bank;
        return true;
    }
    if(bank == files_bank_ && (files_ready_ || files_failed_)) return false;
    target_ = bank;
    BeginFiles();
    return true;
}

uint16_t BankScanner::BankMask() const {
    const unsigned n = BankCount();
    return n >= 16 ? 0xFFFF : uint16_t((1u << n) - 1);
}

uint16_t BankScanner::SlotMask() const {
    if(!files_ready_) return 0;
    const unsigned n = files_.Count();
    return n >= 16 ? 0xFFFF : uint16_t((1u << n) - 1);
}

bool BankScanner::FolderPath(unsigned bank, char* out, size_t cap) const {
    if(bank < 1 || bank > BankCount() || !cap) return false;
    size_t used = 0;
    out[0] = 0;
    if(!Append(out, cap, used, kSamplesRoot)) return false;
    if(loose_ && bank == 1) return true;
    const unsigned i = bank - 1 - (loose_ ? 1 : 0);
    return Append(out, cap, used, "/") && Append(out, cap, used, OpenName(folders_.Name(i), folders_.Alt(i)));
}

bool BankScanner::Path(unsigned slot, char* out, size_t cap) const {
    if(!files_ready_ || slot < 1 || slot > files_.Count()) return false;
    if(!FolderPath(files_bank_, out, cap)) return false;
    size_t used = std::strlen(out);
    return Append(out, cap, used, "/")
           && Append(out, cap, used, OpenName(files_.Name(slot - 1), files_.Alt(slot - 1)));
}

void BankScanner::BeginFiles() {
    files_.Clear();
    files_ready_ = files_failed_ = false;
    pending_ = 0;
    if(target_ < 1 || target_ > BankCount()) target_ = 1;
    if(!BankCount()) { files_bank_ = 0; phase_ = Phase::Idle; return; }
    files_bank_ = target_;
    phase_ = Phase::Files;
}

void BankScanner::Fail() {
    phase_ = Phase::Error;
    discovered_ = true;
}

bool BankScanner::Step(Directory& dir) {
    switch(phase_) {
    case Phase::Idle:
    case Phase::Error:
        return false;
    case Phase::Root: {
        if(!open_) {
            if(!dir.Open(kSamplesRoot)) {  // no SAMPLES folder: a card without banks
                discovered_ = true;
                BeginFiles();
                return Busy();
            }
            open_ = true;
            candidates_.Clear();
            return true;
        }
        const int r = dir.Next(entry_);
        if(r < 0) { dir.Close(); open_ = false; Fail(); return false; }
        if(r == 0) {
            dir.Close(); open_ = false;
            probe_ = 0;
            probe_more_ = candidates_.Count() == kMaxSlots;
            phase_ = Phase::Probe;
            return true;
        }
        if(SkippedEntry(entry_)) return true;
        if(entry_.directory) {
            if(!bounded_ || NaturalCompare(entry_.name, bound_) > 0)
                candidates_.Insert(entry_.name, entry_.alt);
        } else if(IsWavName(entry_.name)) {
            loose_ = true;
        }
        return true;
    }
    case Phase::Probe: {
        if(BankCount() >= kMaxBanks || (probe_ >= candidates_.Count() && !probe_more_)) {
            if(open_) { dir.Close(); open_ = false; }
            discovered_ = true;
            BeginFiles();
            return Busy();
        }
        if(probe_ >= candidates_.Count()) {  // more folders beyond the 16 candidates
            Copy(bound_, candidates_.Name(candidates_.Count() - 1), kNameBytes);
            bounded_ = true;
            phase_ = Phase::Root;
            return true;
        }
        if(!open_) {
            char path[kPathBytes];
            size_t used = 0;
            path[0] = 0;
            if(!Append(path, sizeof path, used, kSamplesRoot) || !Append(path, sizeof path, used, "/")
               || !Append(path, sizeof path, used,
                          OpenName(candidates_.Name(probe_), candidates_.Alt(probe_)))
               || !dir.Open(path)) {
                ++probe_;  // unreadable folder: not a bank
                return true;
            }
            open_ = true;
            return true;
        }
        const int r = dir.Next(entry_);
        if(r <= 0) { dir.Close(); open_ = false; ++probe_; return true; }  // empty or unreadable
        if(!SkippedEntry(entry_) && !entry_.directory && IsWavName(entry_.name)) {
            dir.Close(); open_ = false;
            folders_.Insert(candidates_.Name(probe_), candidates_.Alt(probe_));
            ++probe_;
        }
        return true;
    }
    case Phase::Files: {
        if(pending_) {
            if(open_) { dir.Close(); open_ = false; }
            target_ = pending_;
            BeginFiles();
            return Busy();
        }
        if(!open_) {
            char path[kPathBytes];
            if(!FolderPath(files_bank_, path, sizeof path) || !dir.Open(path)) {
                files_failed_ = true;
                phase_ = Phase::Idle;
                return false;
            }
            open_ = true;
            return true;
        }
        const int r = dir.Next(entry_);
        if(r < 0) {
            dir.Close(); open_ = false;
            files_.Clear();
            files_failed_ = true;
            phase_ = Phase::Idle;
            return false;
        }
        if(r == 0) {
            dir.Close(); open_ = false;
            files_ready_ = true;
            phase_ = Phase::Idle;
            return false;
        }
        if(!SkippedEntry(entry_) && !entry_.directory && IsWavName(entry_.name))
            files_.Insert(entry_.name, entry_.alt);
        return true;
    }
    }
    return false;
}

} // namespace neb
