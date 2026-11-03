// Boot intent: the one-shot record that selects the boot personality after a controlled
// software reset (performance or SD management). Generalised from Nebulae on Daisy 0.2.0
// (instruments/granular/src/boot_intent.h): every firmware passes its own magic, so an
// intent written by one image is never taken by another. Pure and host-tested; the
// firmware maps it onto backup SRAM at kBootIntentAddress.
//
// Rules:
//  * Only a record with this firmware's magic, the version, a known personality and a
//    valid checksum selects SD management. Everything else, including power-on garbage,
//    boots PERFORMANCE.
//  * The firmware takes the record (reads and clears it) before deciding anything, so a
//    crash or power cycle in SD management always comes back as PERFORMANCE.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace sdshare {

// Backup SRAM is 4 KiB at 0x38800000; libDaisy's boot_info (12 bytes, shared with the
// external bootloader) sits at its start. The firmware's linker script asserts the gap.
constexpr uintptr_t kBootIntentAddress = 0x38800F00u;

enum class Personality : uint8_t { Performance = 0, SdManagement = 1 };

// Two application bytes survive the restart (Nebulae: bank and slot; 0 = none).
struct BootIntent {
    Personality personality = Personality::Performance;
    uint8_t a = 0, b = 0;
};

struct BootIntentWords {
    uint32_t word[4];
};

constexpr uint32_t kBootIntentVersion = 1;
constexpr uint32_t Magic(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) << 24 | uint32_t(uint8_t(b)) << 16 | uint32_t(uint8_t(c)) << 8 | uint8_t(d);
}

inline uint32_t BootIntentCheck(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = 2166136261u;  // FNV-1a over the three payload words
    for(uint32_t w : {a, b, c})
        for(int i = 0; i < 4; ++i) {
            h ^= (w >> (8 * i)) & 0xFFu;
            h *= 16777619u;
        }
    return h;
}

inline BootIntentWords EncodeBootIntent(uint32_t magic, const BootIntent& intent) {
    BootIntentWords w{};
    w.word[0] = magic;
    w.word[1] = kBootIntentVersion << 16 | uint32_t(intent.personality);
    w.word[2] = uint32_t(intent.a) | uint32_t(intent.b) << 8;
    w.word[3] = BootIntentCheck(w.word[0], w.word[1], w.word[2]);
    return w;
}

// `limit`: the largest valid value of each application byte.
inline bool DecodeBootIntent(uint32_t magic, const BootIntentWords& w, BootIntent& out, uint8_t limit = 255) {
    out = BootIntent{};
    if(w.word[0] != magic || (w.word[1] >> 16) != kBootIntentVersion
       || w.word[3] != BootIntentCheck(w.word[0], w.word[1], w.word[2])
       || (w.word[2] >> 16) != 0 || ((w.word[1] >> 8) & 0xFFu) != 0)
        return false;
    const uint32_t personality = w.word[1] & 0xFFu;
    const uint32_t a = w.word[2] & 0xFFu, b = (w.word[2] >> 8) & 0xFFu;
    if(personality > uint32_t(Personality::SdManagement) || a > limit || b > limit)
        return false;
    out.personality = Personality(personality);
    out.a = uint8_t(a);
    out.b = uint8_t(b);
    return true;
}

inline BootIntent TakeBootIntent(uint32_t magic, volatile uint32_t* words, uint8_t limit = 255) {
    BootIntentWords w{};
    for(int i = 0; i < 4; ++i) w.word[i] = words[i];
    for(int i = 0; i < 4; ++i) words[i] = 0;
    BootIntent intent;
    DecodeBootIntent(magic, w, intent, limit);
    return intent;
}

inline void StoreBootIntent(uint32_t magic, volatile uint32_t* words, const BootIntent& intent) {
    const BootIntentWords w = EncodeBootIntent(magic, intent);
    for(int i = 0; i < 4; ++i) words[i] = w.word[i];
}

} // namespace sdshare
