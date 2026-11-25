// Edits around the SHARE decision: nothing may change between ACCEPTED and StopAudio(),
// and an edit that is still on its way refuses the share (docs/SD_MANAGEMENT.md).
// Runs the production main.cpp, as tests/test_callback.cpp does.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include "memory_volume.h"
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cstdio>
#include <vector>

static MemoryVolume memoryCard;
namespace looper {
bool InitCard(bool (*present)()) { return present != nullptr; }
Volume& CardVolume() { return memoryCard; }
}  // namespace looper
#include "sd_management_fakes.h"

constexpr size_t kBlock = 48;
static void Audio(unsigned count, float amplitude = 0.f) {
    float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
    const float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    for (unsigned b = 0; b < count; ++b) {
        for (size_t i = 0; i < kBlock; ++i) {
            il[i] = amplitude * ((i & 1) ? 1.f : -1.f);
            ir[i] = il[i];
        }
        AudioCallback(in, out, kBlock);
        ++System::now;
    }
}
static void Blocks(unsigned count, float amplitude = 0.f) {
    for (unsigned b = 0; b < count; ++b) {
        Audio(1, amplitude);
        ServiceStorage(daisy::System::now);
    }
}
static void Press(unsigned key, float amplitude = 0.f) {
    hw.pressed[key] = true;
    Blocks(15, amplitude);
    hw.pressed[key] = false;
    Blocks(15, amplitude);
}
static daisy::MidiEvent Cc(uint8_t controller, uint8_t value) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange;
    e.data[0] = controller;
    e.data[1] = value;
    return e;
}
static daisy::MidiEvent Share() {
    const uint8_t body[] = {0x7D, 0x4C, 0x50, 0x01, 0x02, 0x00, 0x11, 0x22, 0x33, 0x44};
    daisy::MidiEvent e;
    e.type = daisy::SystemCommon;
    e.sc_type = daisy::SystemExclusive;
    std::copy(std::begin(body), std::end(body), e.sysex_data);
    e.sysex_message_len = uint8_t(sizeof(body));
    return e;
}
static int LastShareResult() {
    return usbMidi.sent.empty() || usbMidi.sent.back().size() != 12 ? -1 : int(usbMidi.sent.back()[6]);
}
static const char* Name(int r) {
    switch (r) {
    case 0: return "ACCEPTED";
    case 1: return "CONFIRM_REQUIRED";
    case 2: return "BUSY";
    case 5: return "SAVING";
    default: return "other";
    }
}

static void Fresh() {
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES);
    memoryCard.files.clear();
    fadeOut.store(false);
    faded.store(false);
    fadeLeft = -1;
    heldPresses.store(0);
    shareRequested = false;
    cardPresent.store(hw.sd_detect.Pressed());
    InitStorage();
    hw.pot[knobs::Varispeed].value = .4f;
    hw.pot[knobs::Inertia].value = 0.f;
    hw.pot[knobs::Slope].value = .5f;
    hw.pot[knobs::Size].value = 1.f;
    Blocks(20);
    Press(keys::Record, .5f);
    Blocks(500, .5f);
    Press(keys::Record, .5f);
    Blocks(3000);  // saved
    usbMidi.sent.clear();
}

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

// Sends SHARE through the main loop; HandleControl's waits run real callbacks.
static int SendShare() {
    usbMidi.incoming.push_back(Share());
    ServiceUsbMidi();
    return LastShareResult();
}
// After a refusal the edit must reach the engine and audio must come back.
static void CheckRefused(const char* name, int r) {
    std::printf("%-44s reply=%s\n", name, Name(r));
    CHECK(r == looper::sysex::kSaving || r == looper::sysex::kBusy);
    CHECK(!shareRequested && !fadeOut.load());
    Blocks(30);
    CHECK(!faded.load() && fadeLeft == -1 && heldPresses.load() == 0);
}

int main() {
    hw.Init();
#if LOOPER_MOD
    InitModulation(48000.f);
#endif
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    System::on_delay = [] { Audio(1); };

    // R0: nothing pending: still ACCEPTED, the edit hold stays until the restart.
    Fresh();
    int r = SendShare();
    std::printf("%-44s reply=%s\n", "R0 saved and idle", Name(r));
    CHECK(r == looper::sysex::kAccepted && shareRequested && fadeOut.load() && faded.load());
    const uint32_t before = engine.ContentVersion();
    hw.pressed[keys::Record] = true;
    Audio(30);  // a press after ACCEPTED changes nothing
    hw.pressed[keys::Record] = false;
    CHECK(engine.ContentVersion() == before && !engine.Overdubbing());
    PrepareShare();
    CHECK(restarting && engine.ContentVersion() == store.SavedVersion());
    restarting = false;
    const unsigned stops = hw.seed.stops;

    // R1: S5 Erase taken, 3 ms into the 10 ms ramp: the erase survives and is saved.
    Fresh();
    hw.pressed[keys::Erase] = true;
    for (unsigned i = 0; i < 40 && !engine.OperationPending(); ++i)
        Blocks(1);
    hw.pressed[keys::Erase] = false;
    Audio(3);
    CheckRefused("R1 erase key, SHARE 3 ms into the ramp", SendShare());
    CHECK(engine.GetState() == looper::State::Empty);
    r = SendShare();  // the panel's retry
    for (unsigned i = 0; i < 100 && r != looper::sysex::kAccepted; ++i) {
        fadeOut.store(false);
        Blocks(5);
        r = SendShare();
    }
    CHECK(r == looper::sysex::kAccepted && memoryCard.files.count("0:/looper/loop.wav") == 0);

    // R2: undo on a saved overdub.
    Fresh();
    Press(keys::Record, .3f);
    Blocks(200, .3f);
    Press(keys::Record);
    Blocks(3000);
    usbMidi.sent.clear();
    hw.pressed[keys::Undo] = true;
    for (unsigned i = 0; i < 40 && !engine.OperationPending(); ++i)
        Blocks(1);
    hw.pressed[keys::Undo] = false;
    CheckRefused("R2 undo key, SHARE right after the press", SendShare());
    CHECK(engine.CanRedo());

    // R3: CC erase ahead of SHARE in one USB batch.
    Fresh();
    usbMidi.incoming.push_back(Cc(106, 127));
    CheckRefused("R3 CC106 erase then SHARE, same batch", SendShare());
    CHECK(engine.GetState() == looper::State::Empty);

    // R4: CC record behind SHARE in the batch: ACCEPTED came first, the press is dropped.
    Fresh();
    usbMidi.incoming.push_back(Share());
    usbMidi.incoming.push_back(Cc(102, 127));
    ServiceUsbMidi();
    r = LastShareResult();
    std::printf("%-44s reply=%s\n", "R4 SHARE then CC102 record, same batch", Name(r));
    const uint32_t decided = engine.ContentVersion();
    Audio(3);
    CHECK(r == looper::sysex::kAccepted && engine.ContentVersion() == decided);
    CHECK(engine.ContentVersion() == store.SavedVersion());

    // R5: a key whose debounced edge lands inside the hold.
    Fresh();
    uint32_t start = engine.ContentVersion();
    hw.pressed[keys::Record] = true;
    unsigned lead = 0;
    for (; lead < 40 && engine.ContentVersion() == start; ++lead)
        Audio(1);
    hw.pressed[keys::Record] = false;
    Fresh();
    hw.pressed[keys::Record] = true;
    Audio(lead - 1);
    r = SendShare();
    hw.pressed[keys::Record] = false;
    CheckRefused("R5 record key edge inside the hold", r);
    CHECK(engine.Overdubbing());

    CHECK(hw.seed.stops == stops);  // only the accepted share of R0 stopped the audio
    if (failures) {
        std::printf("FAILED: %d share handoff check(s)\n", failures);
        return 1;
    }
    std::printf("PASS share handoff\n");
    return 0;
}
