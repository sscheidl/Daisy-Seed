#pragma once
// Control SysEx of the instruments that hand their microSD to Windows: Nebulae on Daisy
// ("NB", instruments/granular/docs/CONTROL_PROTOCOL.md) and the Looper ("LP",
// instruments/looper/docs/SD_MANAGEMENT.md). Port of diagnostics/windows/daisy_diag/
// nebulae.py and looper.py; the byte layouts are pinned in docs/dcp/SD_SHARE_PORT.md § 2.1
// and cross-checked against the Python reference by tests/sd_share_vectors.txt.
// Header-only, no JUCE.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace editor::sd {
enum class Instrument { Nebulae, Looper };

constexpr uint8_t kVersion = 0x01, kCmdStatus = 0x01, kCmdShare = 0x02, kCmdError = 0x7F, kReplyBit = 0x40;
constexpr std::size_t kNebulaeStatusLength = 19, kLooperStatusLength = 13;

// SHARE results. Saving exists only for the Looper; Nebulae reports code 5 as Unknown.
enum class ShareResult { Accepted = 0, ConfirmRequired = 1, Busy = 2, NoCard = 3, BadRequest = 4, Saving = 5, Unknown = 127 };

inline std::string ShareResultName(ShareResult r, uint8_t code) {
    switch(r) {
        case ShareResult::Accepted: return "ACCEPTED";
        case ShareResult::ConfirmRequired: return "CONFIRM_REQUIRED";
        case ShareResult::Busy: return "BUSY";
        case ShareResult::NoCard: return "NO_CARD";
        case ShareResult::BadRequest: return "BAD_REQUEST";
        case ShareResult::Saving: return "SAVING";
        case ShareResult::Unknown: break;
    }
    return "CODE" + std::to_string(code);
}

inline std::string ErrorCodeName(uint8_t code) {
    switch(code) {
        case 1: return "VERSION";
        case 2: return "UNKNOWN";
        case 3: return "LENGTH";
        default: return "CODE" + std::to_string(code);
    }
}

// Texts shown on the SD card page (SD_SHARE_PORT.md § 6.1), per instrument.
struct Texts {
    const char* busy;
    const char* confirm;
    const char* back;
    const char* returning;
    const char* local;
    const char* shared;
    const char* ejected;
    const char* saving;
};

// What differs between instruments that share their card the same way.
struct Profile {
    Instrument id;
    const char* name;            // shown in messages: "Nebulae", "Looper"
    const char* display_name;    // "Nebulae on Daisy", "Looper"
    const char* app;             // HELLO app= in SD management
    const char* capability;      // DDP capability of the status message
    const char* status_message;  // DDP message type: "NEB" / "LOOPER"
    const char* perform_command; // DDP command back to performance
    uint8_t owner[2];            // bytes after F0 7D
    std::size_t status_length;
    Texts texts;
};

inline const Profile kNebulae{
    Instrument::Nebulae, "Nebulae", "Nebulae on Daisy", "NebulaeOnDaisy", "nebmgmt", "NEB", "NEB a=PERFORM",
    {0x4E, 0x42}, kNebulaeStatusLength,
    {"Nebulae is recording or loading a sample. Stop it and try again.",
     "A recorded Live loop exists only in RAM and will be LOST by the restart. Confirm to continue, or cancel and "
     "keep playing.",
     "Back in performance. Banks were rescanned from the card.",
     "Nebulae restarts into performance and rescans the card …",
     "Card is with Nebulae (not shared). Share it with Windows or return to performance.",
     "Windows owns the card. Copy, rename or delete samples, then use “Safely Remove Hardware” / Eject in Windows.",
     "Windows ejected the card and all writes completed. You can return to Nebulae.",
     "Saving before the restart …"}};

inline const Profile kLooper{
    Instrument::Looper, "Looper", "Looper", "LooperOnDaisy", "loopmgmt", "LOOPER", "LOOPER a=PERFORM",
    {0x4C, 0x50}, kLooperStatusLength,
    {"The looper is recording, overdubbing, resampling or importing a WAV file. Stop it and try again.",
     "The loop could not be saved to the card (the last write failed). Sharing restarts the looper and LOSES the "
     "unsaved changes; looper/loop.wav keeps the last saved loop. Confirm to continue, or cancel and keep playing.",
     "Back in performance. The loop was reloaded from looper/loop.wav.",
     "The looper restarts into performance and reloads looper/loop.wav …",
     "Card is with the looper (not shared). Share it with Windows or return to performance.",
     "Windows owns the card. Copy WAV files into looper/loops or back up looper/loop.wav, then use “Safely Remove "
     "Hardware” / Eject in Windows.",
     "Windows ejected the card and all writes completed. You can return to the looper.",
     "Saving the current loop to looper/loop.wav before the restart …"}};

inline const Profile* const kProfiles[] = {&kNebulae, &kLooper};
constexpr std::size_t kProfileCount = 2;

inline const Profile* ProfileForApp(const std::string& app) {
    for(const Profile* p : kProfiles)
        if(app == p->app)
            return p;
    return nullptr;
}

// STATUS reply of the performance personality. The Nebulae and Looper fields are both
// present; `instrument` says which ones the firmware filled.
struct PerfStatus {
    Instrument instrument = Instrument::Nebulae;
    std::string firmware;
    bool card = false, overrun = false;
    // Nebulae
    bool live = false, recording = false, loading = false, scanning = false;
    int bank = 0, banks = 0, playing_bank = 0, playing_slot = 0, bank_mask = 0, slot_mask = 0, load_error = 0;
    // Looper
    bool loop = false, unsaved = false, saving = false, importing = false, save_failed = false;
    int fault = 0, loop_ms = 0;
    // both (firmware counters, not bench measurements)
    int overruns = 0, peak_load = 0;

    bool Busy() const { return instrument == Instrument::Nebulae ? (recording || loading) : (recording || importing); }
    std::string LoadErrorText() const {
        static const char* const texts[] = {"none", "missing file", "invalid WAV", "file too large", "SD I/O error", "no card"};
        return load_error >= 0 && load_error < 6 ? texts[load_error] : "code " + std::to_string(load_error);
    }
    std::string FaultText() const {
        static const char* const texts[] = {"none", "card not ready", "write failed", "looper busy", "no WAV files", "file rejected"};
        return fault >= 0 && fault < 6 ? texts[fault] : "code " + std::to_string(fault);
    }
};

struct Reply {
    enum Kind { Status, Share, Error } kind = Status;
    PerfStatus status;
    ShareResult result = ShareResult::Unknown;
    uint8_t result_code = 0;
    uint8_t token[4]{};
    uint8_t error_command = 0, error_code = 0;
};

inline std::vector<uint8_t> StatusRequest(const Profile& p) {
    return {0xF0, 0x7D, p.owner[0], p.owner[1], kVersion, kCmdStatus, 0xF7};
}

// false: a token byte is not 7-bit (nothing is written).
inline bool ShareRequest(const Profile& p, bool confirm, const uint8_t token[4], std::vector<uint8_t>& out) {
    for(int i = 0; i < 4; ++i)
        if(token[i] > 0x7F)
            return false;
    out = {0xF0, 0x7D, p.owner[0], p.owner[1], kVersion, kCmdShare, uint8_t(confirm ? 1 : 0),
           token[0], token[1], token[2], token[3], 0xF7};
    return true;
}

inline int Mask14(const uint8_t* b) { return b[0] | (b[1] << 7) | ((b[2] & 0x03) << 14); }

// One complete message F0 … F7 of this profile. false: not this profile's reply, or
// malformed; `error` names the reason as the Python parser does.
inline bool ParseReply(const Profile& p, const uint8_t* m, std::size_t n, Reply& out, std::string* error = nullptr) {
    auto fail = [&](const char* why) {
        if(error)
            *error = why;
        return false;
    };
    if(n < 8 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != p.owner[0] || m[3] != p.owner[1] || m[n - 1] != 0xF7)
        return fail(p.id == Instrument::Nebulae ? "not a Nebulae SysEx reply" : "not a looper SysEx reply");
    const uint8_t* body = m + 4;
    const std::size_t body_length = n - 5;
    for(std::size_t i = 0; i < body_length; ++i)
        if(body[i] > 0x7F)
            return fail("status byte inside SysEx");
    if(body[0] != kVersion)
        return fail("unsupported protocol version");
    const uint8_t command = body[1];
    const uint8_t* data = body + 2;
    const std::size_t length = body_length - 2;
    out = Reply{};
    if(command == (kCmdStatus | kReplyBit)) {
        if(length != p.status_length)
            return fail("STATUS reply has the wrong length");
        if(data[3] != 0)
            return fail("STATUS from an unknown personality");
        PerfStatus& s = out.status;
        s.instrument = p.id;
        s.firmware = std::to_string(data[0]) + "." + std::to_string(data[1]) + "." + std::to_string(data[2]);
        const uint8_t flags = data[4];
        s.card = flags & 1;
        if(p.id == Instrument::Nebulae) {
            s.live = flags & 2;
            s.recording = flags & 4;
            s.loading = flags & 8;
            s.overrun = flags & 16;
            s.scanning = flags & 32;
            s.bank = data[5];
            s.banks = data[6];
            s.playing_bank = data[7];
            s.playing_slot = data[8];
            s.bank_mask = Mask14(data + 9);
            s.slot_mask = Mask14(data + 12);
            s.load_error = data[15];
            s.overruns = data[16] | (data[17] << 7);
            s.peak_load = data[18];
        } else {
            s.loop = flags & 2;
            s.unsaved = flags & 4;
            s.saving = flags & 8;
            s.recording = flags & 16;
            s.importing = flags & 32;
            s.overrun = flags & 64;
            s.save_failed = data[5] & 1;
            s.fault = data[6];
            s.loop_ms = data[7] | (data[8] << 7) | (data[9] << 14);
            s.overruns = data[10] | (data[11] << 7);
            s.peak_load = data[12];
        }
        out.kind = Reply::Status;
        return true;
    }
    if(command == (kCmdShare | kReplyBit)) {
        if(length != 5)
            return fail("SHARE reply has the wrong length");
        out.kind = Reply::Share;
        out.result_code = data[0];
        const bool known = data[0] <= 4 || (data[0] == 5 && p.id == Instrument::Looper);
        out.result = known ? ShareResult(data[0]) : ShareResult::Unknown;
        for(int i = 0; i < 4; ++i)
            out.token[i] = data[1 + i];
        return true;
    }
    if(command == (kCmdError | kReplyBit)) {
        if(length != 2)
            return fail("ERROR reply has the wrong length");
        out.kind = Reply::Error;
        out.error_command = data[0];
        out.error_code = data[1];
        return true;
    }
    return fail("unknown reply command");
}

// The profile whose parser accepts the message, or nullptr for foreign SysEx (Mod on Daisy
// monitor frames, SQ, the bootloader reply): those are skipped, not errors.
inline const Profile* ParseAny(const uint8_t* m, std::size_t n, Reply& out) {
    for(const Profile* p : kProfiles)
        if(ParseReply(*p, m, n, out))
            return p;
    return nullptr;
}

// Whether a complete SysEx frame belongs to one of the SD share protocols at all.
inline bool IsSdShareSysEx(const uint8_t* m, std::size_t n) {
    if(n < 5 || m[0] != 0xF0 || m[1] != 0x7D)
        return false;
    for(const Profile* p : kProfiles)
        if(m[2] == p->owner[0] && m[3] == p->owner[1])
            return true;
    return false;
}
}
