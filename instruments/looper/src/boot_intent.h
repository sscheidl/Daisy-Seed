// SPDX-License-Identifier: MIT
// Boot intent: the one-shot record that selects the boot personality after a
// controlled software reset (docs/SD_MANAGEMENT.md). Pure and host-tested; the
// firmware maps it onto backup SRAM at kBootIntentAddress. Same scheme as Nebulae on
// Daisy (instruments/granular/src/boot_intent.h), with the looper's own magic, so an
// intent written by one image never selects a mode in the other.
//
// Rules:
//  * Only a record with valid magic, version, personality and checksum selects SD
//    management. Everything else, including power-on garbage, boots PERFORMANCE.
//  * main() takes the record (reads and clears it) before any other work, so a crash
//    or power cycle in SD management always comes back as PERFORMANCE.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace looper::boot {

// Backup SRAM is 4 KiB at 0x38800000; libDaisy's boot_info (12 bytes, shared with
// the external bootloader) sits at its start. memory.ld asserts the gap.
constexpr uintptr_t kBootIntentAddress = 0x38800F00u;

enum class Personality : uint8_t { Performance = 0, SdManagement = 1 };

struct BootIntentWords {
    uint32_t word[4];
};

constexpr uint32_t kBootIntentMagic = 0x4C505231u;  // "LPR1"
constexpr uint32_t kBootIntentVersion = 1;

inline uint32_t BootIntentCheck(uint32_t a, uint32_t b, uint32_t c) {
    // FNV-1a over the three payload words; a stale or random record fails it.
    uint32_t h = 2166136261u;
    for (uint32_t w : {a, b, c})
        for (int i = 0; i < 4; ++i) {
            h ^= (w >> (8 * i)) & 0xFFu;
            h *= 16777619u;
        }
    return h;
}

inline BootIntentWords EncodeBootIntent(Personality personality) {
    BootIntentWords w{};
    w.word[0] = kBootIntentMagic;
    w.word[1] = kBootIntentVersion << 16 | uint32_t(personality);
    w.word[2] = 0;  // reserved
    w.word[3] = BootIntentCheck(w.word[0], w.word[1], w.word[2]);
    return w;
}

// PERFORMANCE for anything but a valid record.
inline Personality DecodeBootIntent(const BootIntentWords& w) {
    if (w.word[0] != kBootIntentMagic || (w.word[1] >> 16) != kBootIntentVersion ||
        ((w.word[1] >> 8) & 0xFFu) != 0 || w.word[2] != 0 ||
        w.word[3] != BootIntentCheck(w.word[0], w.word[1], w.word[2]))
        return Personality::Performance;
    return (w.word[1] & 0xFFu) == uint32_t(Personality::SdManagement) ? Personality::SdManagement
                                                                       : Personality::Performance;
}

// Reads and clears the record in one step. `words` is the backup SRAM location.
inline Personality TakeBootIntent(volatile uint32_t* words) {
    BootIntentWords w{};
    for (int i = 0; i < 4; ++i)
        w.word[i] = words[i];
    for (int i = 0; i < 4; ++i)
        words[i] = 0;
    return DecodeBootIntent(w);
}

inline void StoreBootIntent(volatile uint32_t* words, Personality personality) {
    const BootIntentWords w = EncodeBootIntent(personality);
    for (int i = 0; i < 4; ++i)
        words[i] = w.word[i];
}

}  // namespace looper::boot
