// Boot intent: the one-shot record that selects the boot personality after a
// controlled software reset (V0_2_ARCHITECTURE.md, decision D1). Pure and host-tested;
// main.cpp maps it onto backup SRAM at kBootIntentAddress.
//
// Rules:
//  * Only a record with valid magic, version, personality and checksum selects SD
//    management. Everything else, including power-on garbage, boots PERFORMANCE.
//  * main() takes the record (reads it and clears it) before any other work, so a
//    crash or power cycle in SD management always comes back as PERFORMANCE.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace neb {

// Backup SRAM is 4 KiB at 0x38800000; libDaisy's boot_info (12 bytes, shared with the
// external bootloader) sits at its start. memory.ld asserts the gap.
constexpr uintptr_t kBootIntentAddress = 0x38800F00u;

enum class Personality : uint8_t { Performance = 0, SdManagement = 1 };

struct BootIntent {
    Personality personality = Personality::Performance;
    uint8_t bank = 0;  // 1..16 to restore after returning, 0 = default (bank 1)
    uint8_t slot = 0;  // 1..16 to restore, 0 = first occupied slot
};

struct BootIntentWords {
    uint32_t word[4];
};

constexpr uint32_t kBootIntentMagic = 0x4E454231u;  // "NEB1"
constexpr uint32_t kBootIntentVersion = 1;

inline uint32_t BootIntentCheck(uint32_t a, uint32_t b, uint32_t c) {
    // FNV-1a over the three payload words; a stale or random record fails it.
    uint32_t h = 2166136261u;
    for(uint32_t w : {a, b, c})
        for(int i = 0; i < 4; ++i) {
            h ^= (w >> (8 * i)) & 0xFFu;
            h *= 16777619u;
        }
    return h;
}

inline BootIntentWords EncodeBootIntent(const BootIntent& intent) {
    BootIntentWords w{};
    w.word[0] = kBootIntentMagic;
    w.word[1] = kBootIntentVersion << 16 | uint32_t(intent.personality);
    w.word[2] = uint32_t(intent.bank) | uint32_t(intent.slot) << 8;
    w.word[3] = BootIntentCheck(w.word[0], w.word[1], w.word[2]);
    return w;
}

// Returns false (and a PERFORMANCE default) for anything but a valid record.
inline bool DecodeBootIntent(const BootIntentWords& w, BootIntent& out) {
    out = BootIntent{};
    if(w.word[0] != kBootIntentMagic || (w.word[1] >> 16) != kBootIntentVersion
       || w.word[3] != BootIntentCheck(w.word[0], w.word[1], w.word[2])
       || (w.word[2] >> 16) != 0 || ((w.word[1] >> 8) & 0xFFu) != 0)
        return false;
    const uint32_t personality = w.word[1] & 0xFFu;
    const uint32_t bank = w.word[2] & 0xFFu, slot = (w.word[2] >> 8) & 0xFFu;
    if(personality > uint32_t(Personality::SdManagement) || bank > 16 || slot > 16)
        return false;
    out.personality = Personality(personality);
    out.bank = uint8_t(bank);
    out.slot = uint8_t(slot);
    return true;
}

// Reads and clears the record in one step. `words` is the backup SRAM location.
inline BootIntent TakeBootIntent(volatile uint32_t* words) {
    BootIntentWords w{};
    for(int i = 0; i < 4; ++i) w.word[i] = words[i];
    for(int i = 0; i < 4; ++i) words[i] = 0;
    BootIntent intent;
    DecodeBootIntent(w, intent);
    return intent;
}

inline void StoreBootIntent(volatile uint32_t* words, const BootIntent& intent) {
    const BootIntentWords w = EncodeBootIntent(intent);
    for(int i = 0; i < 4; ++i) words[i] = w.word[i];
}

} // namespace neb
