// SPDX-License-Identifier: MIT
// Control SysEx (STATUS, SHARE decision and framing) and the boot intent record.
#include "boot_intent.h"
#include "control_sysex.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace looper;
static unsigned checks = 0;
#define CHECK(x)                                                               \
    do {                                                                       \
        ++checks;                                                              \
        if (!(x)) {                                                            \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);          \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

static std::vector<uint8_t> Run(std::vector<uint8_t> body, const sysex::Status& s, int& result,
                                bool& sent) {
    body.insert(body.begin(), {0x7D, 0x4C, 0x50});
    sysex::Reply reply(0);
    sent = sysex::Handle(body.data(), body.size(), s, reply, result);
    const uint8_t* bytes = reply.Finish();
    return sent ? std::vector<uint8_t>(bytes, bytes + reply.Size()) : std::vector<uint8_t>{};
}

static void Decision() {
    sysex::Status s;
    s.card = true;
    CHECK(sysex::DecideShare(s, false) == sysex::kAccepted);  // empty or saved loop
    s.loop = true;
    CHECK(sysex::DecideShare(s, false) == sysex::kAccepted);
    s.unsaved = true;
    CHECK(sysex::DecideShare(s, false) == sysex::kSaving);  // save first
    s.saving = true;
    CHECK(sysex::DecideShare(s, false) == sysex::kSaving);
    s.save_failed = true;  // a retry is running: still saving
    CHECK(sysex::DecideShare(s, false) == sysex::kSaving);
    s.saving = false;  // saving is impossible
    CHECK(sysex::DecideShare(s, false) == sysex::kConfirmRequired);
    CHECK(sysex::DecideShare(s, true) == sysex::kAccepted);
    s.card = false;  // the card comes before the unsaved check
    CHECK(sysex::DecideShare(s, true) == sysex::kNoCard);
    s.card = true;
    s.recording = true;  // busy always wins, also with confirm
    CHECK(sysex::DecideShare(s, true) == sysex::kBusy);
    s.recording = false;
    s.importing = true;
    CHECK(sysex::DecideShare(s, true) == sysex::kBusy);
    s.card = false;
    CHECK(sysex::DecideShare(s, false) == sysex::kBusy);
}

static void Framing() {
    sysex::Status s;
    s.fw[0] = 0;
    s.fw[1] = 5;
    s.fw[2] = 0;
    s.card = s.loop = s.overrun = true;
    s.fault = 2;
    s.loop_ms = 87000;  // 87 s
    s.overruns = 20000;  // saturates at 14 bits
    s.peak_load = 200;   // saturates at 127
    int result = 0;
    bool sent = false;
    std::vector<uint8_t> r = Run({0x01, 0x01}, s, result, sent);
    CHECK(sent && result == -1 && r.size() == 6 + sysex::kStatusPayload + 1);
    CHECK(r[0] == 0xF0 && r[1] == 0x7D && r[2] == 0x4C && r[3] == 0x50 && r[4] == 1 && r[5] == 0x41);
    CHECK(r[6] == 0 && r[7] == 5 && r[8] == 0 && r[9] == 0);
    CHECK(r[10] == (1 | 2 | 64) && r[11] == 0 && r[12] == 2);
    CHECK((r[13] | r[14] << 7 | r[15] << 14) == 87000);
    CHECK((r[16] | r[17] << 7) == 16383 && r[18] == 127 && r.back() == 0xF7);
    for (size_t i = 1; i + 1 < r.size(); ++i)
        CHECK(r[i] < 0x80);
    s.unsaved = s.saving = s.recording = s.importing = s.save_failed = true;
    s.overrun = false;
    s.loop_ms = 1u << 22;  // saturates at 21 bits
    r = Run({0x01, 0x01}, s, result, sent);
    CHECK(r[10] == (1 | 2 | 4 | 8 | 16 | 32) && r[11] == 1);
    CHECK((r[13] | r[14] << 7 | r[15] << 14) == 0x1FFFFF);

    // SHARE echoes the token after the result.
    sysex::Status ok;
    ok.card = true;
    r = Run({0x01, 0x02, 0x00, 0x01, 0x7F, 0x00, 0x55}, ok, result, sent);
    CHECK(sent && result == sysex::kAccepted && r.size() == 12 && r[5] == 0x42 && r[6] == 0);
    CHECK(r[7] == 0x01 && r[8] == 0x7F && r[9] == 0x00 && r[10] == 0x55 && r[11] == 0xF7);
    ok.unsaved = true;
    r = Run({0x01, 0x02, 0x00, 1, 2, 3, 4}, ok, result, sent);
    CHECK(result == sysex::kSaving && r[6] == sysex::kSaving);
    r = Run({0x01, 0x02, 0x01, 1, 2, 3, 4}, ok, result, sent);
    CHECK(result == sysex::kAccepted);
    // Malformed SHARE: BAD_REQUEST, no action.
    r = Run({0x01, 0x02, 0x02, 1, 2, 3, 4}, ok, result, sent);
    CHECK(sent && result == -1 && r[6] == sysex::kBadRequest && r[7] == 1 && r[10] == 4);
    r = Run({0x01, 0x02, 0x00, 1, 2, 3}, ok, result, sent);
    CHECK(sent && result == -1 && r[6] == sysex::kBadRequest && r[7] == 0);
    // Errors: version, unknown command, STATUS with a payload.
    r = Run({0x02, 0x01}, ok, result, sent);
    CHECK(sent && r.size() == 9 && r[5] == 0x7F && r[6] == 0x01 && r[7] == sysex::kErrVersion);
    r = Run({0x01, 0x33}, ok, result, sent);
    CHECK(sent && r[6] == 0x33 && r[7] == sysex::kErrUnknown);
    r = Run({0x01, 0x01, 0x00}, ok, result, sent);
    CHECK(sent && r[7] == sysex::kErrLength && result == -1);
    // Not ours or too short/long: no reply.
    Run({0x01}, ok, result, sent);
    CHECK(!sent);
    Run(std::vector<uint8_t>(20, 0x01), ok, result, sent);
    CHECK(!sent);
    const uint8_t nebulae[] = {0x7D, 0x4E, 0x42, 0x01, 0x01};
    const uint8_t boot[] = {0x7D, 0x42, 0x4C, 0x01, 0x01, 'B', 'O', 'O', 'T'};
    CHECK(!sysex::IsControl(nebulae, sizeof nebulae) && !sysex::IsControl(boot, sizeof boot));
    CHECK(!sysex::IsControl(boot, 2));
}

static void BootIntent() {
    volatile uint32_t words[4] = {0, 0, 0, 0};
    CHECK(boot::TakeBootIntent(words) == boot::Personality::Performance);  // cleared SRAM
    boot::StoreBootIntent(words, boot::Personality::SdManagement);
    CHECK(words[0] == boot::kBootIntentMagic);
    CHECK(boot::TakeBootIntent(words) == boot::Personality::SdManagement);
    CHECK(words[0] == 0 && words[1] == 0 && words[2] == 0 && words[3] == 0);  // one-shot
    CHECK(boot::TakeBootIntent(words) == boot::Personality::Performance);
    // Any single changed bit selects PERFORMANCE.
    const boot::BootIntentWords good = boot::EncodeBootIntent(boot::Personality::SdManagement);
    for (int w = 0; w < 4; ++w)
        for (int bit = 0; bit < 32; ++bit) {
            boot::BootIntentWords bad = good;
            bad.word[w] ^= 1u << bit;
            CHECK(boot::DecodeBootIntent(bad) == boot::Personality::Performance);
        }
    // Nebulae's record (magic "NEB1") never selects the looper's SD management.
    boot::BootIntentWords neb = good;
    neb.word[0] = 0x4E454231u;
    neb.word[3] = boot::BootIntentCheck(neb.word[0], neb.word[1], neb.word[2]);
    CHECK(boot::DecodeBootIntent(neb) == boot::Personality::Performance);
    // Garbage patterns.
    uint32_t seed = 1;
    for (int i = 0; i < 10000; ++i) {
        boot::BootIntentWords g;
        for (auto& v : g.word) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            v = seed;
        }
        CHECK(boot::DecodeBootIntent(g) == boot::Personality::Performance);
    }
    CHECK(boot::DecodeBootIntent(boot::EncodeBootIntent(boot::Personality::Performance)) ==
          boot::Personality::Performance);
}

int main() {
    Decision();
    Framing();
    BootIntent();
    std::printf("PASS control SysEx and boot intent: %u checks; share decision, framing, errors, one-shot record\n",
                checks);
}
