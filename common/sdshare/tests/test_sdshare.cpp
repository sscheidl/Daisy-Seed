// common/sdshare: boot intent with per-firmware magic, and the shared SysEx frame.
#include "boot_intent.h"
#include "control_sysex.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
using namespace sdshare;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while(0)

static void Intent() {
    constexpr uint32_t kLooper = Magic('L', 'P', 'R', '1'), kNebulae = Magic('N', 'E', 'B', '1');
    CHECK(kNebulae == 0x4E454231u);  // same record as Nebulae 0.2.0's boot_intent.h
    volatile uint32_t words[4] = {0, 0, 0, 0};
    CHECK(TakeBootIntent(kLooper, words).personality == Personality::Performance);
    StoreBootIntent(kLooper, words, {Personality::SdManagement, 0, 0});
    // Another firmware's image never takes it, and taking clears it either way.
    CHECK(TakeBootIntent(kNebulae, words).personality == Personality::Performance);
    for(int i = 0; i < 4; ++i) CHECK(words[i] == 0);
    StoreBootIntent(kLooper, words, {Personality::SdManagement, 7, 9});
    BootIntent i = TakeBootIntent(kLooper, words);
    CHECK(i.personality == Personality::SdManagement && i.a == 7 && i.b == 9);
    CHECK(TakeBootIntent(kLooper, words).personality == Personality::Performance);  // one-shot
    StoreBootIntent(kNebulae, words, {Personality::SdManagement, 17, 1});
    CHECK(TakeBootIntent(kNebulae, words, 16).personality == Personality::Performance);  // limit
    const BootIntentWords good = EncodeBootIntent(kLooper, {Personality::SdManagement, 1, 2});
    for(int w = 0; w < 4; ++w)
        for(int b = 0; b < 32; ++b) {
            BootIntentWords bad = good;
            bad.word[w] ^= 1u << b;
            BootIntent out;
            CHECK(!DecodeBootIntent(kLooper, bad, out) && out.personality == Personality::Performance);
        }
    std::mt19937 rng(3);
    for(int n = 0; n < 100000; ++n) {
        BootIntentWords r{{uint32_t(rng()), uint32_t(rng()), uint32_t(rng()), uint32_t(rng())}};
        BootIntent out;
        CHECK(!DecodeBootIntent(kLooper, r, out));
    }
}

static std::vector<uint8_t> Bytes(sysex::Reply& r) {
    const uint8_t* b = r.Finish();
    std::vector<uint8_t> v(b, b + r.Size());
    CHECK(v.front() == 0xF0 && v.back() == 0xF7);
    for(size_t i = 1; i + 1 < v.size(); ++i) CHECK(v[i] < 0x80);
    return v;
}

static void Frame() {
    const sysex::Prefix lp{'L', 'P'};
    const uint8_t status[] = {0x7D, 'L', 'P', 1, 1};
    const uint8_t nebulae[] = {0x7D, 'N', 'B', 1, 1};
    const uint8_t boot[] = {0x7D, 0x42, 0x4C, 1, 1, 'B', 'O', 'O', 'T'};
    CHECK(sysex::IsOurs(status, 5, lp) && !sysex::IsOurs(nebulae, 5, lp) && !sysex::IsOurs(boot, 9, lp));
    sysex::ShareRequest share;
    sysex::Reply reply(lp, 0);
    CHECK(sysex::Parse(status, 5, lp, share, reply) == sysex::kStatus);
    const uint8_t shareBody[] = {0x7D, 'L', 'P', 1, 2, 1, 9, 8, 7, 6};
    CHECK(sysex::Parse(shareBody, 10, lp, share, reply) == sysex::kShare && share.confirm);
    CHECK(share.token[0] == 9 && share.token[3] == 6);
    sysex::ShareReply(reply, lp, sysex::kPending, share);
    auto v = Bytes(reply);
    CHECK((v == std::vector<uint8_t>{0xF0, 0x7D, 'L', 'P', 1, 0x42, 5, 9, 8, 7, 6, 0xF7}));
    const uint8_t badConfirm[] = {0x7D, 'L', 'P', 1, 2, 2, 1, 2, 3, 4};
    CHECK(sysex::Parse(badConfirm, 10, lp, share, reply) == sysex::kError);
    v = Bytes(reply);
    CHECK(v[5] == 0x42 && v[6] == sysex::kBadRequest && v[7] == 1);
    const uint8_t version[] = {0x7D, 'L', 'P', 2, 1};
    CHECK(sysex::Parse(version, 5, lp, share, reply) == sysex::kError);
    v = Bytes(reply);
    CHECK(v[5] == 0x7F && v[6] == 1 && v[7] == sysex::kErrVersion);
    const uint8_t longStatus[] = {0x7D, 'L', 'P', 1, 1, 0};
    CHECK(sysex::Parse(longStatus, 6, lp, share, reply) == sysex::kError && Bytes(reply)[7] == sysex::kErrLength);
    const uint8_t unknown[] = {0x7D, 'L', 'P', 1, 0x33};
    CHECK(sysex::Parse(unknown, 5, lp, share, reply) == sysex::kError && Bytes(reply)[7] == sysex::kErrUnknown);
    sysex::Reply u(lp, sysex::kStatus);
    u.U14(20000).Mask16(0xFFFF);
    v = Bytes(u);
    CHECK(v[6] == 0x7F && v[7] == 0x7F && v[8] == 0x7F && v[9] == 0x7F && v[10] == 3);
}

int main() {
    Intent();
    Frame();
    std::printf("PASS sdshare boot intent and SysEx frame: %u checks\n", checks);
}
