// SPDX-License-Identifier: MIT
// SHARE decision for every input combination, and the STATUS payload layout.
#include "share_control.h"
#include <cstdio>
#include <cstdlib>
using namespace looper::share;
using namespace sdshare::sysex;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    for (unsigned m = 0; m < 64; ++m) {
        Inputs in{bool(m & 1), bool(m & 2), bool(m & 4), bool(m & 8), bool(m & 16)};
        const bool confirm = m & 32;
        const ShareResult r = Decide(in, confirm);
        if (!in.card) CHECK(r == kNoCard);
        else if (in.importing || !in.engineIdle) CHECK(r == kBusy);
        else if (in.inSync) CHECK(r == kAccepted);           // loop already on the card
        else if (confirm) CHECK(r == kAccepted);             // user accepts losing the change
        else if (in.saveFailed) CHECK(r == kConfirmRequired);
        else CHECK(r == kPending);                           // save first, then restart
        CHECK(r != kAccepted || in.inSync || confirm);       // never lose an unsaved loop silently
    }
    Status s;
    s.fw[0] = 0; s.fw[1] = 5; s.fw[2] = 0;
    s.card = s.loop = s.inSync = s.overrun = true;
    s.sharePending = true;
    s.loopDeciseconds = 871; s.fault = 2; s.overruns = 20000; s.peakLoad = 200;
    Reply r(kPrefix, kStatus);
    WriteStatus(r, s);
    const uint8_t* b = r.Finish();
    CHECK(r.Size() == 6 + kStatusPayload + 1);
    CHECK(b[2] == 'L' && b[3] == 'P' && b[5] == 0x41 && b[7] == 5 && b[9] == 0);
    CHECK(b[10] == (1 | 2 | 32 | 64) && b[11] == 1);
    CHECK((b[12] | b[13] << 7) == 871 && b[14] == 2 && b[15] == 0x7F && b[16] == 0x7F && b[17] == 127);
    std::printf("PASS share decision: 64 input combinations, STATUS layout (%u checks)\n", checks);
}
