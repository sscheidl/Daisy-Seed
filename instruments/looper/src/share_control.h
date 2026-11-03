// SPDX-License-Identifier: MIT
// SD sharing for the Looper (v0.5): the decision for a SHARE request and the STATUS
// payload of the control SysEx `F0 7D 4C 50 ...` ("LP", common/sdshare/control_sysex.h).
// Pure, host-tested (tests/test_share.cpp). docs/SD_MANAGEMENT.md has the protocol.
#pragma once
#include "control_sysex.h"
#include <cstdint>

namespace looper::share {

constexpr sdshare::sysex::Prefix kPrefix{'L', 'P'};
constexpr uint32_t kSaveTimeoutMs = 120000;  // a pending share gives up after this

struct Inputs {
    bool card = false;        // card in the slot and mounted
    bool engineIdle = false;  // not recording, resampling, fading or copying
    bool importing = false;   // a WAV import is running
    bool inSync = false;      // loop.wav holds exactly the current loop (or none after erase)
    bool saveFailed = false;  // the last save failed (card full, write error)
};

// SHARE needs the current loop on the card first (decision of 2026-11-03): in sync ->
// restart now; otherwise save first (PENDING) and restart once in sync. Only when that
// save failed does the panel need the user's confirmation to share anyway.
inline sdshare::sysex::ShareResult Decide(const Inputs& in, bool confirm) {
    using namespace sdshare::sysex;
    if (!in.card) return kNoCard;
    if (in.importing || !in.engineIdle) return kBusy;
    if (in.inSync || confirm) return kAccepted;
    if (in.saveFailed) return kConfirmRequired;
    return kPending;
}

struct Status {
    uint8_t fw[3] = {};
    bool card = false, loop = false, busy = false, saving = false, importing = false;
    bool inSync = false, sharePending = false, shareFailed = false, overrun = false;
    uint32_t loopDeciseconds = 0;  // loop length in 0.1 s
    uint8_t fault = 0;             // LoopStore::Fault
    uint32_t overruns = 0;
    uint8_t peakLoad = 0;          // percent of the block deadline
};
constexpr unsigned kStatusPayload = 12;

// STATUS reply payload (12 bytes):
//   0-2 firmware, 3 personality (0 = performance),
//   4 flags A: bit0 card, 1 loop present, 2 engine busy, 3 saving, 4 importing,
//              5 card in sync (loop saved), 6 overrun latched
//   5 flags B: bit0 share pending (saving before the restart), bit1 share failed
//   6-7 loop length in 0.1 s (14 bit), 8 store fault, 9-10 overruns (14 bit), 11 peak %
inline void WriteStatus(sdshare::sysex::Reply& r, const Status& s) {
    r.Data(s.fw[0]).Data(s.fw[1]).Data(s.fw[2]).Data(0);
    r.Data(uint8_t((s.card ? 1 : 0) | (s.loop ? 2 : 0) | (s.busy ? 4 : 0) | (s.saving ? 8 : 0)
                   | (s.importing ? 16 : 0) | (s.inSync ? 32 : 0) | (s.overrun ? 64 : 0)));
    r.Data(uint8_t((s.sharePending ? 1 : 0) | (s.shareFailed ? 2 : 0)));
    r.U14(s.loopDeciseconds).Data(s.fault).U14(s.overruns).Data(s.peakLoad > 127 ? 127 : s.peakLoad);
}

}  // namespace looper::share
