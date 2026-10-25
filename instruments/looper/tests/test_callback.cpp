// SPDX-License-Identifier: MIT
// Run the production AudioCallback and Controls() with libDaisy's pinned
// DigitalControl debouncer and a fake kit (4 s buffers, see the Makefile).
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include "memory_volume.h"
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }  // the USB state the firmware reads
#include <cmath>
#include <cstdio>

// The card behind main.cpp's storage code: in memory, always mounted.
static MemoryVolume memoryCard;
namespace looper {
bool InitCard(bool (*present)()) { return present != nullptr; }
Volume& CardVolume() { return memoryCard; }
}  // namespace looper

static std::vector<uint8_t> Pcm16Stereo(uint32_t frames, int16_t value) {
    std::vector<uint8_t> b;
    auto put = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF");
    put(36 + frames * 4, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2);
    put(2, 2);
    put(48000, 4);
    put(48000 * 4, 4);
    put(4, 2);
    put(16, 2);
    tag("data");
    put(frames * 4, 4);
    for (uint32_t i = 0; i < frames * 2; ++i)
        put(uint16_t(value), 2);
    return b;
}

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

constexpr size_t kBlock = 48;
static uint64_t t = 0;
static float peak = 0.f, loopEnergy = 0.f;
static bool allFinite = true;

static void Blocks(unsigned count, float amplitude = 0.f) {
    float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
    const float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    for (unsigned b = 0; b < count; ++b) {
        for (size_t i = 0; i < kBlock; ++i, ++t) {
            il[i] = amplitude * float(std::sin(2.0 * 3.14159265358979 * 220.0 * double(t) / 48000.0));
            ir[i] = -1.f;  // ignored: no plug in the right input
        }
        AudioCallback(in, out, kBlock);
        ServiceStorage(daisy::System::now);  // the main loop between callbacks
        for (size_t i = 0; i < kBlock; ++i) {
            allFinite &= std::isfinite(ol[i]) && std::isfinite(orr[i]);
            peak = std::max({peak, std::fabs(ol[i]), std::fabs(orr[i])});
            loopEnergy += std::fabs(ol[i] - il[i]);
        }
        ++System::now;  // 48 samples = 1 ms
    }
}
static void Press(unsigned key, float amplitude = 0.f) {
    hw.pressed[key] = true;
    Blocks(15, amplitude);
    hw.pressed[key] = false;
    Blocks(15, amplitude);
}

int main() {
    hw.Init();
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    CHECK(engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES));
    memoryCard.files["0:/looper/loops/one.wav"] = Pcm16Stereo(24000, 8192);  // 0.5 s at 0.25
    cardPresent.store(hw.sd_detect.Pressed());
    InitStorage();
    CHECK(storageReady && engine.GetState() == looper::State::Empty);  // nothing saved yet
    hw.pot[knobs::Varispeed].value = .4f;  // unity
    hw.pot[knobs::Inertia].value = 0.f;
    hw.pot[knobs::Slope].value = .5f;
    hw.pot[knobs::Size].value = 1.f;  // whole loop
    Blocks(20);
    CHECK(engine.VarispeedTarget() == 0.f && engine.GetState() == looper::State::Empty);

    // Record a first take of about one second, then stop.
    Press(keys::Record, .5f);
    CHECK(engine.GetState() == looper::State::Recording);
    Blocks(1000, .5f);
    Press(keys::Record, .5f);
    CHECK(engine.GetState() == looper::State::Looping);
    CHECK(engine.Length() > 46000 && engine.Length() < 50000);
    CHECK((uiState.load() & ui::HasLoop) && (uiState.load() & ui::CanUndo));
    loopEnergy = 0.f;
    Blocks(500);
    CHECK(loopEnergy > 100.f);  // silent input, the loop plays

    // Play/pause.
    Press(keys::Play);
    CHECK(!engine.Playing() && !(uiState.load() & ui::Playing));
    Press(keys::Play);
    CHECK(engine.Playing());

    // Knobs: varispeed -24..+36 semitones, inertia 0..1.
    hw.pot[knobs::Varispeed].value = 1.f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 36.f);
    hw.pot[knobs::Varispeed].value = 0.f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == -24.f);
    hw.pot[knobs::Varispeed].value = .5f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 6.f);
    hw.pot[knobs::Varispeed].value = .4f;
    hw.pot[knobs::Inertia].value = .7f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 0.f && engine.Inertia() == .7f);
    hw.pot[knobs::Inertia].value = 0.f;

    // Reverse toggles.
    Press(keys::Reverse);
    Blocks(20);
    CHECK(engine.Reversed() && (uiState.load() & ui::Reversed));
    Press(keys::Reverse);
    Blocks(20);
    CHECK(!engine.Reversed());

    // Overdub, then undo and redo with the same key.
    Press(keys::Record, .3f);
    CHECK(engine.Overdubbing());
    Blocks(300, .3f);
    Press(keys::Record);
    Blocks(50);
    CHECK(!engine.Overdubbing() && engine.CanUndo());
    Press(keys::Undo);
    CHECK(engine.CanRedo() && !engine.CanUndo() && (uiState.load() & ui::CanRedo));
    Press(keys::Undo);
    CHECK(engine.CanUndo() && !engine.CanRedo());

    // Erase is undoable.
    Press(keys::Erase);
    CHECK(engine.GetState() == looper::State::Empty && !(uiState.load() & ui::HasLoop));
    Press(keys::Undo);
    CHECK(engine.GetState() == looper::State::Looping);

    // Mode keys: S6 cycles, S9..S12 select directly.
    auto mode = [] { return (uiState.load() >> ui::kModeShift) & 3u; };
    Press(keys::LoopMode);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Replace && mode() == 1);
    Press(keys::kFirstMode + 3);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Resample && mode() == 3);
    Press(keys::LoopMode);
    CHECK(engine.GetLoopMode() == looper::LoopMode::SoundOnSound && mode() == 0);
    // Frippertronics: decay pot as in the manual, undo key locked.
    Press(keys::kFirstMode + 2);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Frippertronics && mode() == 2);
    CHECK(!engine.CanUndo() && !(uiState.load() & (ui::CanUndo | ui::CanRedo)));
    const uint32_t before = engine.Length();
    Press(keys::Undo);
    Blocks(50);
    CHECK(!engine.OperationPending() && engine.Length() == before && !engine.CanRedo());
    hw.pot[knobs::Decay].value = 1.f;
    Blocks(200);
    CHECK(engine.FrippertronicsDecay() == 1.f && engine.FrippertronicsRetention() < 1e-3f);
    hw.pot[knobs::Decay].value = 0.f;
    Blocks(200);
    CHECK(std::fabs(engine.FrippertronicsRetention() - looper::kMaxRetention) < 1e-3f);
    Press(keys::kFirstMode);
    CHECK(engine.CanRedo());  // the redo of the erase above is visible again
    // Slice, Skip, Start and Size pots.
    hw.pot[knobs::Slice].value = 1.f;
    hw.pot[knobs::Skip].value = .5f;
    Blocks(2);
    CHECK(engine.SliceDivisor() == 16);
    hw.pot[knobs::Slice].value = 0.f;
    hw.pot[knobs::Skip].value = 0.f;
    hw.pot[knobs::Start].value = .5f;
    hw.pot[knobs::Size].value = .5f;
    Blocks(20);
    CHECK(engine.WindowStart() == engine.Length() / 2 && engine.WindowSize() < engine.Length());
    hw.pot[knobs::Start].value = 0.f;
    hw.pot[knobs::Size].value = 1.f;
    Blocks(20);
    CHECK(engine.WindowSize() == engine.Length());
    // Resample with the record key: the loop is replaced and undoable.
    Press(keys::kFirstMode + 3);
    Press(keys::Record);
    CHECK(engine.Resampling() && (uiState.load() & ui::Recording));
    Blocks(500);
    Press(keys::Record);
    Blocks(100);
    CHECK(!engine.Resampling() && engine.Length() > 500 * 48 && engine.Length() < 560 * 48);
    Press(keys::Undo);
    CHECK(engine.Length() == before);
    Press(keys::kFirstMode + 1);
    Press(keys::Seam);
    CHECK(engine.GetSeamMode() == looper::SeamMode::Crossfade && (uiState.load() & ui::Crossfade));

    // A held key with contact dropouts does not retrigger.
    const bool reversed = engine.Reversed();
    hw.pressed[keys::Reverse] = true;
    Blocks(15);
    for (unsigned dropout = 1; dropout <= 6; ++dropout) {
        hw.pressed[keys::Reverse] = false;
        Blocks(dropout);
        hw.pressed[keys::Reverse] = true;
        Blocks(15);
    }
    hw.pressed[keys::Reverse] = false;
    Blocks(20);
    CHECK(engine.Reversed() != reversed);

    // MIDI CC on J7, any channel: CC102-113 act like S1-S12, CC20-27 like the pots.
    auto cc = [](uint8_t controller, uint8_t value, int channel = 0) {
        daisy::MidiEvent e;
        e.type = daisy::ControlChange;
        e.channel = channel;
        e.data[0] = controller;
        e.data[1] = value;
        usbMidi.incoming.push_back(e);
        ServiceUsbMidi();  // main loop
        Blocks(2);
    };
    auto tap = [&](uint8_t controller, int channel = 0) {
        cc(controller, 127, channel);
        cc(controller, 0, channel);
    };
    const bool midiReversed = engine.Reversed();
    tap(105);
    Blocks(20);
    CHECK(engine.Reversed() != midiReversed);
    cc(105, 127);
    cc(105, 100);  // still held: one press only
    Blocks(20);
    CHECK(engine.Reversed() == midiReversed);
    cc(105, 0);
    tap(113, 15);  // channel 16
    CHECK(engine.GetLoopMode() == looper::LoopMode::Resample && mode() == 3);
    tap(111);
    CHECK(engine.GetLoopMode() == looper::LoopMode::Replace);
    tap(107);  // next mode
    CHECK(engine.GetLoopMode() == looper::LoopMode::Frippertronics);
    tap(110);
    CHECK(engine.GetLoopMode() == looper::LoopMode::SoundOnSound);
    const auto seam = engine.GetSeamMode();
    tap(108);
    CHECK(engine.GetSeamMode() != seam);
    tap(108);
    tap(103);
    CHECK(!engine.Playing());
    tap(103);
    CHECK(engine.Playing());
    tap(102);
    CHECK(engine.Overdubbing());
    Blocks(200, .3f);
    tap(102);
    Blocks(50);
    CHECK(!engine.Overdubbing() && engine.CanUndo());
    tap(104);
    Blocks(50);  // the 10 ms undo ramp
    CHECK(engine.CanRedo());
    tap(104);
    Blocks(50);
    CHECK(engine.CanUndo() && !engine.CanRedo());
    // CC20 sets whole semitones, 64 = unity; the pot takes over again after a 2 % move.
    cc(20, 76);
    CHECK(engine.VarispeedTarget() == 12.f);
    cc(20, 0);
    CHECK(engine.VarispeedTarget() == -24.f);
    cc(20, 127);
    CHECK(engine.VarispeedTarget() == 36.f);
    cc(20, 76);
    hw.pot[knobs::Varispeed].value = .41f;  // 1 %: MIDI keeps the knob
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 12.f);
    hw.pot[knobs::Varispeed].value = .43f;  // 3 %: the pot is back
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 2.f);
    hw.pot[knobs::Varispeed].value = .4f;
    Blocks(2);
    CHECK(engine.VarispeedTarget() == 0.f);
    cc(21, 127);
    CHECK(engine.Inertia() == 1.f);
    cc(21, 0);
    CHECK(engine.Inertia() == 0.f);
    cc(25, 127);
    CHECK(engine.SliceDivisor() == 16);
    cc(25, 0);
    CHECK(engine.SliceDivisor() == 1);
    cc(24, 64);
    Blocks(20);
    CHECK(engine.WindowSize() < engine.Length());
    cc(24, 127);
    Blocks(20);
    CHECK(engine.WindowSize() == engine.Length());
    cc(27, 127);
    CHECK(engine.FrippertronicsDecay() == 1.f);
    hw.pot[knobs::Decay].value = .05f;  // the pot takes the knob back
    Blocks(2);
    CHECK(engine.FrippertronicsDecay() == .05f);
    hw.pot[knobs::Decay].value = 0.f;
    // Other controllers and notes do nothing and get no reply.
    const auto loopMode = engine.GetLoopMode();
    const bool playing = engine.Playing();
    for (uint8_t other : {1, 7, 10, 64, 101, 114, 120, 123})
        tap(other);
    daisy::MidiEvent note;
    note.type = daisy::NoteOn;
    note.data[0] = 60;
    note.data[1] = 100;
    usbMidi.incoming.push_back(note);
    ServiceUsbMidi();
    Blocks(20);
    CHECK(engine.GetLoopMode() == loopMode && engine.Playing() == playing && usbMidi.sent.empty());
    // Every pot takes its knob back (the remaining checks use the pots).
    for (unsigned k = 0; k <= knobs::Decay; ++k) {
        const float v = hw.pot[k].value;
        hw.pot[k].value = v < .5f ? v + .05f : v - .05f;
        Blocks(2);
        hw.pot[k].value = v;
        Blocks(2);
    }
    CHECK(engine.VarispeedTarget() == 0.f && engine.WindowSize() == engine.Length());

    // Recording until the 4 s buffer is full stops automatically.
    Press(keys::Erase);
    Blocks(50);
    Press(keys::Record, .9f);
    Blocks(4100, .9f);
    CHECK(engine.GetState() == looper::State::Looping && engine.Length() == LOOPER_BUFFER_FRAMES);
    Blocks(200, .9f);
    CHECK(allFinite && peak <= 1.f);

    // microSD: the full loop is saved after 2 s idle, S8 imports loops/one.wav,
    // and a power-up recalls the last saved loop.
    Blocks(4000);
    CHECK(memoryCard.files.count("0:/looper/loop.wav") && store.Saves() > 0);
    uint32_t shown = ~0u;
    ShowLeds(daisy::System::now, shown);
    CHECK(store.GetStatus(daisy::System::now) == looper::LoopStore::Status::Idle);
    Press(keys::Import);
    Blocks(100);
    CHECK(engine.Length() == 24000 && engine.StoredFrame(12000).left == .25f);
    CHECK(std::string(store.LastImportName()) == "one.wav" && engine.CanUndo());
    Blocks(3000);
    const uint32_t saved = store.Saves();
    CHECK(saved >= 2);
    CHECK(engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES) && engine.GetState() == looper::State::Empty);
    InitStorage();  // power-up
    CHECK(engine.GetState() == looper::State::Looping && engine.Length() == 24000);
    CHECK(engine.StoredFrame(12000).right == .25f && !engine.CanUndo());

    // Bootloader entry: S9+S16 for 5 s (S9 selects Sound on Sound), never during card I/O.
    hw.pressed[8] = hw.pressed[15] = true;
    Blocks(4990);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 0);
    Blocks(20);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 1 && System::bootloader_mode == System::DAISY_INFINITE_TIMEOUT);
    hw.pressed[8] = hw.pressed[15] = false;
    Blocks(15);
    hw.pressed[keys::Import] = true;
    for (unsigned i = 0; i < 20 && !store.Active(); ++i)
        Blocks(1);
    CHECK(store.Active());
    bootRequest.Set(bootloader::Request::kKeys);
    unsigned waited = 0;
    for (; waited < 1000 && store.Active(); ++waited) {
        ServiceBootloader();
        Blocks(1);
    }
    CHECK(System::bootloader_resets == 1 && waited > 0 && !store.Active());
    ServiceBootloader();
    CHECK(System::bootloader_resets == 2);
    hw.pressed[keys::Import] = false;
    Blocks(15);
    // SysEx over J7 during an import: answered at once, the reset follows when idle.
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    daisy::MidiEvent boot;
    boot.type = daisy::SystemCommon;
    boot.sc_type = daisy::SystemExclusive;
    std::copy(std::begin(bootloader::kRequestBody), std::end(bootloader::kRequestBody), boot.sysex_data);
    boot.sysex_message_len = sizeof(bootloader::kRequestBody);
    hw.pressed[keys::Import] = true;
    for (unsigned i = 0; i < 20 && !store.Active(); ++i)
        Blocks(1);
    CHECK(store.Active());
    usbMidi.incoming.push_back(boot);
    ServiceUsbMidi();
    const std::vector<uint8_t> ack(std::begin(bootloader::kAck), std::end(bootloader::kAck));
    CHECK(usbMidi.sent.size() == 1 && usbMidi.sent[0] == ack);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 2);
    for (waited = 0; waited < 1000 && store.Active(); ++waited)
        Blocks(1);
    ServiceBootloader();
    CHECK(System::bootloader_resets == 3 && usbMidi.sent.size() == 1);  // one reply only
    boot.sysex_message_len--;  // truncated: ignored
    usbMidi.incoming.push_back(boot);
    ServiceUsbMidi();
    ServiceBootloader();
    CHECK(System::bootloader_resets == 3 && usbMidi.sent.size() == 1);
    hw.pressed[keys::Import] = false;
    hUsbDeviceHS.dev_state = 0;
    Blocks(15);

    if (failures) {
        std::printf("FAILED: %d production callback check(s)\n", failures);
        return 1;
    }
    std::printf("PASS production callback: keys, knobs, MIDI CC, LEDs state, auto-stop, bounded output, bootloader keys/SysEx after card I/O\n");
    return 0;
}
