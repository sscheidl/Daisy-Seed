// Boot intent record and the Nebulae control SysEx (CONTROL_PROTOCOL.md).
#include "boot_intent.h"
#include "control_sysex.h"
#include "bootloader_entry.h"
#include "control.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
using namespace neb;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while(0)

static void Intent() {
    volatile uint32_t words[4] = {0, 0, 0, 0};
    CHECK(TakeBootIntent(words).personality == Personality::Performance);  // zeroed SRAM
    StoreBootIntent(words, {Personality::SdManagement, 3, 7});
    BootIntent i = TakeBootIntent(words);
    CHECK(i.personality == Personality::SdManagement && i.bank == 3 && i.slot == 7);
    for(int k = 0; k < 4; ++k) CHECK(words[k] == 0);  // one-shot: cleared when taken
    CHECK(TakeBootIntent(words).personality == Personality::Performance);  // second boot
    StoreBootIntent(words, {Personality::Performance, 16, 16});
    i = TakeBootIntent(words);
    CHECK(i.personality == Personality::Performance && i.bank == 16 && i.slot == 16);
    // Every single-bit corruption is rejected (falls back to performance).
    const BootIntentWords good = EncodeBootIntent({Personality::SdManagement, 2, 5});
    for(int w = 0; w < 4; ++w)
        for(int b = 0; b < 32; ++b) {
            BootIntentWords bad = good;
            bad.word[w] ^= 1u << b;
            BootIntent out;
            CHECK(!DecodeBootIntent(bad, out) && out.personality == Personality::Performance);
        }
    // Out-of-range fields with a valid checksum are rejected too.
    for(uint32_t w2 : {17u, 17u << 8, 1u << 16}) {
        BootIntentWords bad{};
        bad.word[0] = kBootIntentMagic; bad.word[1] = kBootIntentVersion << 16 | 1; bad.word[2] = w2;
        bad.word[3] = BootIntentCheck(bad.word[0], bad.word[1], bad.word[2]);
        BootIntent out;
        CHECK(!DecodeBootIntent(bad, out));
    }
    BootIntentWords bad{};
    bad.word[0] = kBootIntentMagic; bad.word[1] = kBootIntentVersion << 16 | 2;
    bad.word[3] = BootIntentCheck(bad.word[0], bad.word[1], bad.word[2]);
    BootIntent out;
    CHECK(!DecodeBootIntent(bad, out));
    // Random power-on contents never select SD management.
    std::mt19937 rng(7);
    auto u = [&] { return uint32_t(rng()); };
    for(int n = 0; n < 200000; ++n) {
        BootIntentWords r{{u(), u(), u(), u()}};
        CHECK(!DecodeBootIntent(r, out));
    }
}

struct Feed {
    sysex::Collector c;
    bootloader::SysExMatcher boot;
    MidiParser midi;
    std::vector<Midi> notes;
    bool boot_hit = false;
    void Bytes(std::initializer_list<uint8_t> bytes) {
        for(uint8_t b : bytes) {
            c.Byte(b);
            boot_hit |= boot.Byte(b);
            Midi m;
            if(midi.Byte(b, m)) notes.push_back(m);
        }
    }
};
#if NEBULAE_PROFILE
static sysex::Profile test_profile;
static bool profile_reset = false;
#endif
static bool Dispatch(const uint8_t* body, size_t n, const sysex::Status& s, sysex::Reply& r, bool& share) {
#if NEBULAE_PROFILE
    return sysex::Handle(body, n, s, r, share, test_profile, profile_reset);
#else
    return sysex::Handle(body, n, s, r, share);
#endif
}
static std::vector<uint8_t> Reply(const uint8_t* body, size_t n, const sysex::Status& s, bool& share) {
    sysex::Reply r(0);
    CHECK(Dispatch(body, n, s, r, share));
    const uint8_t* bytes = r.Finish();
    std::vector<uint8_t> v(bytes, bytes + r.Size());
    CHECK(v.front() == 0xF0 && v.back() == 0xF7);
    for(size_t i = 1; i + 1 < v.size(); ++i) CHECK(v[i] < 0x80);  // valid SysEx data
    if(v[5] != (sysex::kProfile | sysex::kReplyBit)) CHECK(v.size() <= 48);
    return v;
}

// PROFILE 0x03: answered by the diagnostic build, unknown to the others.
static void ProfileCommand() {
    sysex::Status s;
    bool share = false;
    const uint8_t request[] = {1, 3};
    auto r = Reply(request, 2, s, share);
#if NEBULAE_PROFILE
    for(size_t i = 0; i < sysex::kProfileValues; ++i) test_profile.value[i] = 0x9ABCDEF0u + uint32_t(i);
    test_profile.value[0] = 480000000u;
    r = Reply(request, 2, s, share);
    CHECK(r.size() == 7 + sysex::kProfilePayload && r[5] == 0x43 && !profile_reset && !share);
    for(size_t i = 0; i < sysex::kProfileValues; ++i) {
        uint32_t v = 0;
        for(unsigned k = 0; k < 5; ++k) v |= uint32_t(r[6 + 5 * i + k]) << (7 * k);
        CHECK(v == test_profile.value[i]);
    }
    const uint8_t reset[] = {1, 3, 1}, keep[] = {1, 3, 0}, bad[] = {1, 3, 2}, overlong[] = {1, 3, 1, 1};
    r = Reply(reset, 3, s, share); CHECK(r[5] == 0x43 && profile_reset);
    r = Reply(keep, 3, s, share); CHECK(r[5] == 0x43 && !profile_reset);
    r = Reply(bad, 3, s, share); CHECK(r[5] == 0x7F && r[7] == sysex::kErrLength && !profile_reset);
    r = Reply(overlong, 4, s, share); CHECK(r[5] == 0x7F && r[7] == sysex::kErrLength);
#else
    CHECK(r[5] == 0x7F && r[6] == 3 && r[7] == sysex::kErrUnknown && !share);
#endif
}

static void Protocol() {
    Feed f;
    uint8_t body[sysex::kMaxBody];
    size_t n = 0;
    // STATUS request among notes, running status and real-time bytes.
    f.Bytes({0x90, 60, 100, 0xF0, 0x7D, 0xF8, 0x4E, 0x42, 0x01, 0x01, 0xF7, 62, 100, 0x90, 62, 100});
    // SysEx cancels running status (MIDI 1.0): "62 100" right after F7 is ignored.
    CHECK(f.c.Take(body, n) && n == 2 && body[0] == 1 && body[1] == 1);
    CHECK(!f.c.Take(body, n));
    CHECK(f.notes.size() == 2 && f.notes[1].a == 62 && !f.boot_hit);  // MIDI unaffected
    // Bootloader and Mod on Daisy SysEx are not ours; ours is not theirs.
    f.Bytes({0xF0, 0x7D, 0x42, 0x4C, 0x01, 0x01, 'B', 'O', 'O', 'T', 0xF7});
    CHECK(f.boot_hit && !f.c.Take(body, n));
    f.boot_hit = false;
    f.Bytes({0xF0, 0x7D, 0x4D, 0x44, 0x01, 0x01, 0xF7, 0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x02, 0, 1, 2, 3, 4, 0xF7});
    CHECK(!f.boot_hit && f.c.Take(body, n) && n == 7 && body[1] == 2);
    // Mailbox: a second request before Take() is dropped (the host retries).
    f.Bytes({0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x01, 0xF7, 0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x02, 1, 9, 9, 9, 9, 0xF7});
    CHECK(f.c.Take(body, n) && n == 2 && !f.c.Take(body, n));
    // Aborted, truncated, overlong and prefix-only frames.
    f.Bytes({0xF0, 0x7D, 0x4E, 0x42, 0x01, 0x90, 60, 100});  // status byte aborts
    f.Bytes({0xF0, 0x7D, 0x4E, 0x42, 0x01, 0xF7});           // no command
    f.Bytes({0xF0, 0x7D, 0x4E, 0xF7});
    f.Bytes({0xF0, 0x7D, 0x4E, 0x42, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0xF7});
    CHECK(!f.c.Take(body, n));

    sysex::Status s;
    s.fw[0] = 0; s.fw[1] = 2; s.fw[2] = 0;
    s.card = true; s.bank = 2; s.banks = 3; s.playing_bank = 2; s.playing_slot = 16;
    s.bank_mask = 0x7; s.slot_mask = 0xFFFF; s.overruns = 20000; s.peak_load = 200; s.overrun = true;
    bool share = true;
    const uint8_t status[] = {1, 1};
    auto r = Reply(status, 2, s, share);
    CHECK(!share && r.size() == 6 + sysex::kStatusPayload + 1 && r[5] == 0x41);
    CHECK(r[6] == 0 && r[7] == 2 && r[8] == 0 && r[9] == 0);   // fw 0.2.0, performance
    CHECK(r[10] == (1 | 16));                                  // card, overrun latched
    CHECK(r[11] == 2 && r[12] == 3 && r[13] == 2 && r[14] == 16);
    CHECK(r[15] == 7 && r[16] == 0 && r[17] == 0);             // bank mask 7-bit groups
    CHECK(r[18] == 0x7F && r[19] == 0x7F && r[20] == 3);       // slot mask 0xFFFF
    CHECK(r[22] == 0x7F && r[23] == 0x7F && r[24] == 127);     // saturated counters
    // SHARE decisions, token echoed.
    auto share_req = [&](uint8_t confirm, const sysex::Status& st, uint8_t& result) {
        const uint8_t b[] = {1, 2, confirm, 5, 6, 7, 8};
        auto v = Reply(b, 7, st, share);
        CHECK(v[5] == 0x42 && v[7] == 5 && v[8] == 6 && v[9] == 7 && v[10] == 8);
        result = v[6];
    };
    uint8_t result = 99;
    share_req(0, s, result); CHECK(result == sysex::kAccepted && share);
    s.live = true;
    share_req(0, s, result); CHECK(result == sysex::kConfirmRequired && !share);
    share_req(1, s, result); CHECK(result == sysex::kAccepted && share);
    s.recording = true;
    share_req(1, s, result); CHECK(result == sysex::kBusy && !share);
    s.recording = false; s.loading = true;
    share_req(1, s, result); CHECK(result == sysex::kBusy && !share);
    s.loading = false; s.card = false;
    share_req(1, s, result); CHECK(result == sysex::kNoCard && !share);
    s.card = true;
    share_req(2, s, result); CHECK(result == sysex::kBadRequest && !share);
    const uint8_t shortshare[] = {1, 2, 1};
    r = Reply(shortshare, 3, s, share);
    CHECK(r[6] == sysex::kBadRequest && !share);
    // Version and unknown command errors.
    const uint8_t v2[] = {2, 1};
    r = Reply(v2, 2, s, share);
    CHECK(r[5] == 0x7F && r[6] == 1 && r[7] == sysex::kErrVersion && !share);
    const uint8_t unknown[] = {1, 0x33};
    r = Reply(unknown, 2, s, share);
    CHECK(r[5] == 0x7F && r[6] == 0x33 && r[7] == sysex::kErrUnknown);
    const uint8_t longstatus[] = {1, 1, 0};
    r = Reply(longstatus, 3, s, share);
    CHECK(r[7] == sysex::kErrLength);
    sysex::Reply dummy(0);
    CHECK(!Dispatch(status, 1, s, dummy, share));
}

int main() {
    Intent();
    Protocol();
    ProfileCommand();
#if NEBULAE_PROFILE
    std::printf("PASS boot intent and control SysEx (profile build): %u checks\n", checks);
#else
    std::printf("PASS boot intent and control SysEx: %u checks\n", checks);
#endif
}
