// SPDX-License-Identifier: MIT
// Looper for the Daisy Seed3 Desktop Dev Kit, libDaisy commit in the repository
// README. Engine with loop modes and Slice/Skip, loop persistence and WAV import
// on the microSD card; no effects. USB-MIDI on J7: CC control (midi_control.h), the
// control SysEx for the Windows control panel (control_sysex.h) and the bootloader
// request. A second boot personality hands the card to Windows over USB mass storage
// (sd_management.h, docs/SD_MANAGEMENT.md). MOD=1 (LOOPER_MOD) builds the separate Mod
// on Daisy image with the modulation matrix of docs/MODULATION.md.
#include "boot_intent.h"
#include "card_volume.h"
#include "control_sysex.h"
#include "loop_store.h"
#include "looper_engine.h"
#include "midi_control.h"
#include "version.h"
#if LOOPER_MOD
const char looper_firmware_version[] = "LOOPER-MOD-VERSION=" LOOPER_VERSION;
#else
const char looper_firmware_version[] = "LOOPER-VERSION=" LOOPER_VERSION;
#endif
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "sd_management.h"
#include "usbd_core.h"
#if LOOPER_MOD
#include "modulation.h"
#include "mod_on_daisy_desktop.h"
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
#include <new>
using namespace daisy;

#ifndef LOOPER_BUFFER_FRAMES
#define LOOPER_BUFFER_FRAMES looper::kMaxFrames
#endif
static_assert(LOOPER_BUFFER_FRAMES <= looper::kMaxFrames, "buffer exceeds the engine limit");

namespace keys {
enum : unsigned {
    Record = 0, Play = 1, Undo = 2, Reverse = 3, Erase = 4, LoopMode = 5, Seam = 6, Import = 7
};
// S9..S12 select Sound on Sound, Replace, Frippertronics, Resample directly.
constexpr unsigned kFirstMode = 8, kModes = 4;
}
namespace knobs {
enum : unsigned {
    Varispeed = 0, Inertia = 1, Slope = 2, Start = 3, Size = 4, Slice = 5, Skip = 6, Decay = 7
};
}
namespace ui {
enum : uint32_t {
    Recording = 1, RecordPending = 2, Playing = 4, CanUndo = 8, CanRedo = 16, Reversed = 32,
    HasLoop = 64, Crossfade = 128
};
constexpr unsigned kModeShift = 8;  // two bits of loop mode above the flags
}

static desktop_devkit::Hardware hw;
// Two 87 s stereo float32 buffers, 66,816,000 bytes of the 64 MiB SDRAM.
// .sdram_bss is NOLOAD and only usable after hw.Init(), so the frames are
// constructed (zeroed) in main.
constexpr size_t kBufferBytes = size_t(LOOPER_BUFFER_FRAMES) * sizeof(looper::Frame);
alignas(looper::Frame) static unsigned char DSY_SDRAM_BSS storageA[kBufferBytes];
alignas(looper::Frame) static unsigned char DSY_SDRAM_BSS storageB[kBufferBytes];
static looper::Engine engine;
static uint32_t downMask = 0;
static std::atomic<uint32_t> uiState{0}, maximumCycles{0}, overruns{0};
static uint32_t budgetCycles = 0;
static float settled[knobs::Decay + 1] = {-1, -1, -1, -1, -1, -1, -1, -1};
// Software entry into the Daisy bootloader: S9+S16 for 5 s, or SysEx over J7.
static bootloader::KeyCombo bootKeys;  // audio callback only
static bootloader::Request bootRequest;

// libDaisy's USB device state for the external port (J7 on the carrier).
extern "C" USBD_HandleTypeDef hUsbDeviceHS;
// USB-MIDI on J7: CC20-27 work like the pots, CC102-113 like the keys S1-S12
// (README "MIDI control"). Notes are ignored.
static MidiUsbHandler usbMidi;
static looper::midi::Receiver midiIn;
static looper::midi::Takeover takeover[knobs::Decay + 1];  // audio callback only
static uint8_t midiValue[knobs::Decay + 1] = {};          // audio callback only
static_assert(unsigned(looper::midi::kButtons) == keys::kFirstMode + keys::kModes &&
                  unsigned(looper::midi::Import) == keys::Import &&
                  unsigned(looper::midi::SoundOnSound) == keys::kFirstMode,
              "MIDI buttons follow the key order");
static_assert(unsigned(looper::midi::kKnobs) == knobs::Decay + 1 &&
                  unsigned(looper::midi::Varispeed) == knobs::Varispeed,
              "MIDI knobs follow the pot order");

#if LOOPER_MOD
// Mod on Daisy (separate build, docs/MODULATION.md): one global matrix, ticked once
// per audio block (1 kHz at 48 frames). The main loop handles its SysEx and replies.
constexpr size_t kBlockFrames = 48;  // hw.seed.SetAudioBlockSize
static mod::Module modulation;
static const mod::InstrumentInfo kModInfo = looper::columns::MakeModInfo();
static mod::adapter::UsbMidiSender modSender(usbMidi);
static looper::columns::MixGains mixGains;      // audio callback only
static uint8_t modeOrigin = mod::kModeUnknown;  // audio callback only: keys or CC presses

static void InitModulation(float sampleRate) {
    modulation.Init(kModInfo, sampleRate, kBlockFrames);
    // extended monitor (base values), LFO cross modulation, loop mode in the monitor
    modulation.EnableExtensions({true, true, true});
}

// Main loop: everything the looper's CC map does not own (CC1, aftertouch, CC120/123,
// clock, start, notes, its SysEx) goes to Mod on Daisy.
static void ForwardToModulation(const MidiEvent& event) {
    mod::MidiMessage message;
    if (mod::adapter::ToMessage(event, message))
        modulation.controller.HandleMidi(message);
}
#endif

// microSD: loop.wav is saved from the main loop and recalled at power-up;
// S8 imports the next WAV file of 0:/looper/loops. The transfer buffer must be
// reachable by the SDMMC IDMA, which cannot access DTCM.
alignas(32) static uint8_t DSY_SDRAM_BSS transfer[32768];
// Filter phases for importing 44.1 kHz files (40 KB; DTCM has no room for it).
static float DSY_SDRAM_BSS resampleTable[looper::resample::kTableSize];
// The store (5 KB, main loop only) lives in AXI SRAM rather than in DTCM, which
// keeps the main stack reserve above 12 KiB with the SD management code (memory.ld).
// The section is NOLOAD: the object is constructed here, before main().
#ifdef __arm__
#define LOOPER_AXI_BSS __attribute__((section(".looper_axi")))
#else
#define LOOPER_AXI_BSS
#endif
alignas(looper::LoopStore) static unsigned char storeStorage[sizeof(looper::LoopStore)] LOOPER_AXI_BSS;
static looper::LoopStore& store = *new (storeStorage) looper::LoopStore();
static bool storageReady = false;
static std::atomic<bool> cardPresent{false}, importRequested{false};
// SD management (docs/SD_MANAGEMENT.md): an accepted SHARE fades the output to
// silence within one block, then the main loop restarts into the card reader.
static std::atomic<bool> fadeOut{false}, faded{false};
static int32_t fadeLeft = -1;  // audio callback only: samples left, -1 before the fade
static bool shareRequested = false, restarting = false;  // main loop only

// Engine state for the main loop, published by the audio callback under a
// sequence lock (odd while it is written).
static std::atomic<uint32_t> snapshotSequence{0}, snapshotVersion{0}, snapshotLength{0},
    snapshotTail{0};
static std::atomic<bool> snapshotIdle{false};
static std::atomic<const looper::Frame*> snapshotFrames{nullptr};

static void PublishSnapshot() {
    snapshotSequence.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    snapshotVersion.store(engine.ContentVersion(), std::memory_order_relaxed);
    snapshotIdle.store(!engine.Busy(), std::memory_order_relaxed);
    snapshotFrames.store(engine.ActiveFrames(), std::memory_order_relaxed);
    snapshotLength.store(engine.Length(), std::memory_order_relaxed);
    snapshotTail.store(engine.TailLength(), std::memory_order_relaxed);
    snapshotSequence.fetch_add(1, std::memory_order_release);
}

struct SnapshotView final : looper::EngineView {
    looper::EngineSnapshot Get() override {
        looper::EngineSnapshot s;
        for (;;) {
            const uint32_t before = snapshotSequence.load(std::memory_order_acquire);
            s.version = snapshotVersion.load(std::memory_order_relaxed);
            s.idle = snapshotIdle.load(std::memory_order_relaxed);
            s.frames = snapshotFrames.load(std::memory_order_relaxed);
            s.length = snapshotLength.load(std::memory_order_relaxed);
            s.tail = snapshotTail.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (!(before & 1u) && snapshotSequence.load(std::memory_order_relaxed) == before)
                return s;
        }
    }
};

// Import requests from the main loop, executed at the start of the next audio
// callback. A cancel is kept separately so that a following request cannot
// overwrite it and leave the engine importing.
struct CallbackPort final : looper::ImportPort {
    enum : uint32_t { None, BeginRequest, CommitRequest };
    std::atomic<uint32_t> command{None}, first{0}, second{0};
    std::atomic<bool> cancel{false};
    std::atomic<Reply> reply{Reply::Idle};
    std::atomic<looper::Frame*> buffer{nullptr};

    void Begin(uint32_t frames) override { Request(BeginRequest, frames, 0); }
    void Commit(uint32_t length, uint32_t tail) override { Request(CommitRequest, length, tail); }
    void Cancel() override {
        reply.store(Reply::Idle, std::memory_order_relaxed);
        cancel.store(true, std::memory_order_release);
    }
    Reply Poll(looper::Frame*& out) override {
        const Reply r = reply.load(std::memory_order_acquire);
        out = buffer.load(std::memory_order_relaxed);
        return r;
    }
    void Request(uint32_t c, uint32_t a, uint32_t b) {
        first.store(a, std::memory_order_relaxed);
        second.store(b, std::memory_order_relaxed);
        reply.store(Reply::Pending, std::memory_order_relaxed);
        command.store(c, std::memory_order_release);
    }
    void Execute() {  // audio thread
        if (cancel.exchange(false, std::memory_order_acquire)) {
            engine.CancelImport();
            buffer.store(nullptr, std::memory_order_relaxed);
        }
        const uint32_t c = command.exchange(None, std::memory_order_acquire);
        if (c == BeginRequest) {
            looper::Frame* p = engine.BeginImport(first.load(std::memory_order_relaxed));
            buffer.store(p, std::memory_order_relaxed);
            reply.store(p ? Reply::Granted : Reply::Denied, std::memory_order_release);
        } else if (c == CommitRequest) {
            const bool ok = engine.CommitImport(first.load(std::memory_order_relaxed),
                                                second.load(std::memory_order_relaxed));
            buffer.store(nullptr, std::memory_order_relaxed);
            reply.store(ok ? Reply::Done : Reply::Failed, std::memory_order_release);
        }
    }
};
static SnapshotView view;
static CallbackPort port;
static_assert(std::atomic<looper::ImportPort::Reply>::is_always_lock_free);
static_assert(std::atomic<const looper::Frame*>::is_always_lock_free);

// Knob 0: -24..+36 semitones, rounded to whole semitones; unity at 40 %.
static float VarispeedFromKnob(float k) {
    k = std::min(std::max(k, 0.f), 1.f);
    return std::round(looper::kMinSemitones + (looper::kMaxSemitones - looper::kMinSemitones) * k);
}

// Ignores pot noise below 0.2 % so the window and slices do not jitter;
// the exact endpoints always pass.
static float Settled(unsigned knob) {
    const float v = hw.pot[knob].Value();
    if (std::abs(v - settled[knob]) > .002f || v <= 0.f || v >= 1.f)
        settled[knob] = v;
    return settled[knob];
}

// Pot or MIDI, whichever moved last (normalized 0...1 for all knobs but varispeed).
static bool MidiKnob(unsigned knob, float pot) {
    uint8_t value = 0;
    const bool fresh = midiIn.TakeKnob(knob, value);
    if (fresh)
        midiValue[knob] = value;
    return takeover[knob].UseMidi(pot, fresh);
}
static float Knob(unsigned knob, float pot) {
    return MidiKnob(knob, pot) ? looper::midi::UnitFromCc(midiValue[knob]) : pot;
}

// The engine is owned by the audio callback; controls are read there too.
static void Controls() {
    hw.UpdateAllControls();
    cardPresent.store(hw.sd_detect.Pressed(), std::memory_order_relaxed);
    uint32_t presses = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t bit = 1u << i;
        if (hw.button[i].FallingEdge())
            downMask &= ~bit;
        if (hw.button[i].RisingEdge() && !(downMask & bit)) {
            downMask |= bit;
            presses |= bit;
        }
    }
    if (bootKeys.Process(downMask, System::GetNow()))
        bootRequest.Set(bootloader::Request::kKeys);
#if LOOPER_MOD
    const uint32_t midiPresses = midiIn.TakePresses();
    constexpr uint32_t kModeKeys =
        (1u << keys::LoopMode) | (((1u << keys::kModes) - 1u) << keys::kFirstMode);
    if (presses & kModeKeys)
        modeOrigin = mod::kModeFromDevice;
    else if (midiPresses & kModeKeys)
        modeOrigin = mod::kModeFromMidi;
    presses |= midiPresses;  // CC presses act like key presses
#else
    presses |= midiIn.TakePresses();  // CC presses act like key presses
#endif
    if (presses & (1u << keys::Record))
        engine.PressRecord();
    if (presses & (1u << keys::Play))
        engine.TogglePlay();
    if (presses & (1u << keys::Undo)) {
        if (engine.CanRedo())
            engine.Redo();
        else
            engine.Undo();
    }
    if (presses & (1u << keys::Reverse))
        engine.ToggleReverse();
    if (presses & (1u << keys::Erase))
        engine.Erase();
    if (presses & (1u << keys::LoopMode))
        engine.SetLoopMode(looper::LoopMode((unsigned(engine.GetLoopMode()) + 1) % keys::kModes));
    for (unsigned m = 0; m < keys::kModes; ++m)
        if (presses & (1u << (keys::kFirstMode + m)))
            engine.SetLoopMode(looper::LoopMode(m));
    if (presses & (1u << keys::Import))
        importRequested.store(true, std::memory_order_relaxed);
    if (presses & (1u << keys::Seam))
        engine.SetSeamMode(engine.GetSeamMode() == looper::SeamMode::Window
                               ? looper::SeamMode::Crossfade
                               : looper::SeamMode::Window);
    const float speedPot = hw.pot[knobs::Varispeed].Value();
#if LOOPER_MOD
    // The pot or CC value is the base; the matrix adds its offset, clamped (modulation.h).
    looper::columns::Bases base;
    base.semitones = MidiKnob(knobs::Varispeed, speedPot)
                         ? looper::midi::SemitonesFromCc(midiValue[knobs::Varispeed])
                         : VarispeedFromKnob(speedPot);
    base.inertia = Knob(knobs::Inertia, hw.pot[knobs::Inertia].Value());
    engine.SetInertialSlope(Knob(knobs::Slope, Settled(knobs::Slope)));
    base.start = Knob(knobs::Start, Settled(knobs::Start));
    base.size = Knob(knobs::Size, Settled(knobs::Size));
    base.slice = Knob(knobs::Slice, Settled(knobs::Slice));
    base.skip = Knob(knobs::Skip, Settled(knobs::Skip));
    base.decay = Knob(knobs::Decay, Settled(knobs::Decay));
    mod::Engine& matrix = modulation.engine;
    mod::adapter::ReadHardware(matrix, hw);
    matrix.Tick(kBlockFrames);
    const looper::columns::Values value = looper::columns::Modulate(matrix, base);
    looper::columns::ReportBases(matrix, base);
    matrix.ReportMode(uint8_t(engine.GetLoopMode()), modeOrigin);
    engine.SetVarispeed(value.semitones);
    engine.SetInertia(value.inertia);
    engine.SetStart(value.start);
    engine.SetSize(value.size);
    engine.SetSlice(value.slice);
    engine.SetSkip(value.skip);
    engine.SetFrippertronicsDecay(value.decay);
    mixGains = value.mix;
#else
    engine.SetVarispeed(MidiKnob(knobs::Varispeed, speedPot)
                            ? looper::midi::SemitonesFromCc(midiValue[knobs::Varispeed])
                            : VarispeedFromKnob(speedPot));
    engine.SetInertia(Knob(knobs::Inertia, hw.pot[knobs::Inertia].Value()));
    engine.SetInertialSlope(Knob(knobs::Slope, Settled(knobs::Slope)));
    engine.SetStart(Knob(knobs::Start, Settled(knobs::Start)));
    engine.SetSize(Knob(knobs::Size, Settled(knobs::Size)));
    engine.SetSlice(Knob(knobs::Slice, Settled(knobs::Slice)));
    engine.SetSkip(Knob(knobs::Skip, Settled(knobs::Skip)));
    engine.SetFrippertronicsDecay(Knob(knobs::Decay, Settled(knobs::Decay)));
#endif

    uint32_t state = 0;
    if (engine.GetState() == looper::State::Recording || engine.Overdubbing() ||
        engine.Resampling())
        state |= ui::Recording;
    if (engine.RecordPending())
        state |= ui::RecordPending;
    if (engine.Playing())
        state |= ui::Playing;
    if (engine.CanUndo())
        state |= ui::CanUndo;
    if (engine.CanRedo())
        state |= ui::CanRedo;
    if (engine.Reversed())
        state |= ui::Reversed;
    if (engine.Length() > 0)
        state |= ui::HasLoop;
    state |= uint32_t(engine.GetLoopMode()) << ui::kModeShift;
    if (engine.GetSeamMode() == looper::SeamMode::Crossfade)
        state |= ui::Crossfade;
    uiState.store(state, std::memory_order_relaxed);
}

static float CodecLevel(float x) {
    // Transparent below -0.45 dBFS, soft knee above, for the dry + loop sum.
    const float a = std::abs(x);
    if (a <= .95f)
        return x;
    const float e = a - .95f, y = .95f + .05f * e / (.05f + e);
    return std::copysign(y, x);
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    const uint32_t start = DWT->CYCCNT;
    Controls();
    port.Execute();
    const float* inRight = hw.right_detect.Pressed() ? in[1] : in[0];
    engine.Process(in[0], inRight, out[0], out[1], n);  // loop playback only
    PublishSnapshot();
#if LOOPER_MOD
    // Mix column: dry and loop gains, both exactly 1 without modulation.
    const looper::columns::MixGains mix = mixGains;
    for (size_t i = 0; i < n; ++i) {
        out[0][i] = CodecLevel(mix.dry * in[0][i] + mix.loop * out[0][i]);
        out[1][i] = CodecLevel(mix.dry * inRight[i] + mix.loop * out[1][i]);
    }
#else
    for (size_t i = 0; i < n; ++i) {
        out[0][i] = CodecLevel(in[0][i] + out[0][i]);
        out[1][i] = CodecLevel(inRight[i] + out[1][i]);
    }
#endif
    if (fadeOut.load(std::memory_order_acquire)) {  // never set while performing
        if (fadeLeft < 0)
            fadeLeft = int32_t(n);  // a linear ramp to exactly zero over one block
        const float length = float(n ? n : 1);
        for (size_t i = 0; i < n; ++i) {
            fadeLeft = fadeLeft > 0 ? fadeLeft - 1 : 0;
            const float gain = float(fadeLeft) / length;
            out[0][i] *= gain;
            out[1][i] *= gain;
        }
        if (fadeLeft == 0)
            faded.store(true, std::memory_order_release);
    }
    const uint32_t elapsed = DWT->CYCCNT - start;
    if (elapsed > maximumCycles.load(std::memory_order_relaxed))
        maximumCycles.store(elapsed, std::memory_order_relaxed);
    if (elapsed > budgetCycles)
        overruns.fetch_add(1, std::memory_order_relaxed);
}

static bool CardPresent() { return cardPresent.load(std::memory_order_relaxed); }

// Before audio starts: set up the card and recall the saved loop.
static void InitStorage() {
    storageReady = looper::InitCard(&CardPresent) &&
                   store.Init(&looper::CardVolume(), transfer, sizeof(transfer), LOOPER_BUFFER_FRAMES,
                              "0:/looper");
    if (storageReady) {
        store.EnableResampling(resampleTable, looper::resample::kTableSize);
        store.Recall(engine);
    }
    PublishSnapshot();
}

// Main loop: all card access happens here.
static void ServiceStorage(uint32_t now) {
    if (!storageReady)
        return;
    if (importRequested.exchange(false, std::memory_order_relaxed))
        store.RequestImport();
    store.Service(view, port, now);
}

static void SendMidi(uint8_t* bytes, size_t size) {
    if (hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
        usbMidi.SendMessage(bytes, size);
}

// "major.minor.patch" from version.h at compile time.
constexpr unsigned VersionPart(const char* v, unsigned part) {
    unsigned value = 0;
    for (; *v && part; ++v)
        if (*v == '.')
            --part;
    for (; *v >= '0' && *v <= '9'; ++v)
        value = value * 10 + unsigned(*v - '0');
    return value;
}

// Main loop: the state the control panel sees (STATUS) and SHARE decides on.
static looper::sysex::Status CurrentStatus() {
    looper::sysex::Status st;
    for (unsigned i = 0; i < 3; ++i)
        st.fw[i] = uint8_t(VersionPart(LOOPER_VERSION, i));
    const looper::EngineSnapshot s = view.Get();
    const uint32_t flags = uiState.load(std::memory_order_relaxed);
    st.card = CardPresent();
    st.loop = s.length > 0;
    st.recording = flags & (ui::Recording | ui::RecordPending);
    if (storageReady && store.VersionKnown()) {
        st.unsaved = s.version != store.SavedVersion();
        st.save_failed = store.SaveFailed();
    } else {  // the card was never usable: the loop cannot be saved
        st.unsaved = st.loop;
        st.save_failed = true;
    }
    st.saving = storageReady && store.Saving();
    st.importing = storageReady && store.Importing();
    st.fault = storageReady ? uint8_t(store.LastFault()) : 0;
    st.loop_ms = s.length / 48u;
    const uint32_t o = overruns.load(std::memory_order_relaxed);
    st.overrun = o != 0;
    st.overruns = uint16_t(o > 16383 ? 16383 : o);
    const uint64_t load =
        budgetCycles ? uint64_t(maximumCycles.load(std::memory_order_relaxed)) * 100u / budgetCycles : 0;
    st.peak_load = uint8_t(load > 127 ? 127 : load);
    return st;
}

// Main loop: answers STATUS and SHARE at once. SAVING starts the save without the
// usual 2 s delay; ACCEPTED restarts into SD management after this iteration.
static void HandleControl(const MidiEvent& event) {
    looper::sysex::Reply reply(0);
    int result = -1;
    if (!looper::sysex::Handle(event.sysex_data, event.sysex_message_len, CurrentStatus(), reply,
                               result))
        return;
    const uint8_t* bytes = reply.Finish();
    SendMidi(const_cast<uint8_t*>(bytes), reply.Size());
    if (result == looper::sysex::kSaving)
        store.SaveSoon();
    else if (result == looper::sysex::kAccepted)
        shareRequested = true;
}

static bool IsControlSysEx(const MidiEvent& event) {
    return event.type == SystemCommon && event.sc_type == SystemExclusive &&
           looper::sysex::IsControl(event.sysex_data, event.sysex_message_len);
}

// Main loop. A SysEx request is answered at once, because a card write may hold
// the reset back for longer than an editor waits for the reply.
static void ServiceUsbMidi() {
    usbMidi.Listen();  // restarts reception after an overflow
    while (usbMidi.HasEvents()) {
        const MidiEvent event = usbMidi.PopEvent();
        if (event.type == ControlChange) {
#if LOOPER_MOD
            if (!midiIn.ControlChange(event.data[0], event.data[1]))
                ForwardToModulation(event);  // CC1 and the other controllers
#else
            midiIn.ControlChange(event.data[0], event.data[1]);
#endif
            continue;
        }
        if (IsControlSysEx(event)) {
            HandleControl(event);
            continue;
        }
        if (!bootloader::IsRequest(event)) {
#if LOOPER_MOD
            ForwardToModulation(event);
#endif
            continue;
        }
        uint8_t ack[sizeof(bootloader::kAck)];
        std::copy(std::begin(bootloader::kAck), std::end(bootloader::kAck), ack);
        SendMidi(ack, sizeof(ack));
        bootRequest.Set(bootloader::Request::kKeys);  // already answered
    }
}

// Main loop: a pending request waits until no card write or import is running.
static void ServiceBootloader() {
    if (!storageReady || !store.Active())
        bootloader::Service(bootRequest, hw);
}

static float CardLed(looper::LoopStore::Status status, uint32_t now) {
    switch (status) {
    case looper::LoopStore::Status::NoCard: return 0.f;
    case looper::LoopStore::Status::Idle: return .1f;
    case looper::LoopStore::Status::Error: return (now / 62u) & 1u ? 1.f : 0.f;
    default: return (now / 125u) & 1u ? .5f : 0.f;  // saving or importing
    }
}

// LEDs are updated from the main loop, and only when something changed.
static void ShowLeds(uint32_t now, uint32_t& shown) {
    const uint32_t s = uiState.load(std::memory_order_relaxed);
    const bool blink = (now / 125u) & 1u;
    const auto status = storageReady ? store.GetStatus(now) : looper::LoopStore::Status::NoCard;
    const float card = CardLed(status, now);
    const uint32_t frame = s | (blink ? 1u << 31 : 0u) |
                           (overruns.load(std::memory_order_relaxed) ? 1u << 30 : 0u) |
                           (uint32_t(card * 10.f) << 24);
    if (frame == shown)
        return;
    shown = frame;
    hw.SetLed(keys::Record,
              (s & ui::Recording) ? 1.f : ((s & ui::RecordPending) && blink ? .5f : 0.f));
    hw.SetLed(keys::Play, (s & ui::Playing) ? .5f : 0.f);
    hw.SetLed(keys::Undo, (s & ui::CanRedo) ? .15f : ((s & ui::CanUndo) ? .5f : 0.f));
    hw.SetLed(keys::Reverse, (s & ui::Reversed) ? .5f : 0.f);
    hw.SetLed(keys::Erase, (s & ui::HasLoop) ? .15f : 0.f);
    for (unsigned m = 0; m < keys::kModes; ++m)
        hw.SetLed(keys::kFirstMode + m, ((s >> ui::kModeShift) & 3u) == m ? .5f : 0.f);
    hw.SetLed(keys::Seam, (s & ui::Crossfade) ? .5f : 0.f);
    hw.SetLed(keys::Import, card);
    hw.SetLed(15, (frame & (1u << 30)) ? 1.f : 0.f);
    hw.UpdateLeds();
}

// PREPARE_SHARE (docs/SD_MANAGEMENT.md): silence, stop audio, stop any card write,
// unmount and reset the SD interface, then restart this image into SD management.
// Never returns on the board. The loop comes back from loop.wav after the return.
static void PrepareShare() {
    fadeOut.store(true, std::memory_order_release);
    for (unsigned i = 0; i < 50 && !faded.load(std::memory_order_acquire); ++i)
        System::Delay(1);
    hw.seed.StopAudio();
    store.Halt();  // only a confirmed share can interrupt a save; loop.wav stays intact
    looper::ReleaseCard();
    for (unsigned i = 0; i < 16; ++i)
        hw.SetLed(i, 0.f);
    hw.UpdateLeds();
    hw.UpdateLeds();
    looper::boot::StoreBootIntent(looper_intent_words(), looper::boot::Personality::SdManagement);
    System::Delay(20);  // the SysEx reply leaves before the reset
    restarting = true;
    System::ResetToBootloader(System::BootloaderMode::DAISY_SKIP_TIMEOUT);
}

[[noreturn]] static void Halt() {
    while (true)
        System::Delay(100);
}

int main() {
    asm volatile("" : : "r"(looper_firmware_version));  // Keep the version in the BIN.
    hw.Init();
    // The boot intent is taken (read and cleared) before anything else is decided:
    // without a valid record this is always the performance personality.
    if (looper::boot::TakeBootIntent(looper_intent_words()) ==
        looper::boot::Personality::SdManagement) {
        looper_run_sd_management(hw);
        Halt();
    }
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(48);
#if LOOPER_MOD
    InitModulation(hw.seed.AudioSampleRate());
#endif
    for (auto& p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    if (!engine.Init(hw.seed.AudioSampleRate(), a, b, LOOPER_BUFFER_FRAMES))
        Halt();
    for (int i = 0; i < 50; ++i) {  // settle the debounced card-detect switch
        hw.UpdateAllControls();
        System::Delay(1);
    }
    cardPresent.store(hw.sd_detect.Pressed(), std::memory_order_relaxed);
    InitStorage();
    budgetCycles = (SystemCoreClock / 48000u) * 48u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usbConfig;
    usbConfig.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usbMidi.Init(usbConfig);
    usbMidi.StartReceive();
    hw.seed.StartAudio(AudioCallback);
    uint32_t shown = ~0u, ledsAt = 0;
    while (!restarting) {
        const uint32_t now = System::GetNow();
        ServiceStorage(now);
        ServiceUsbMidi();
        if (shareRequested) {
            shareRequested = false;
            PrepareShare();
            continue;
        }
#if LOOPER_MOD
        modulation.controller.Service(System::GetNow(), modSender);
#endif
        ServiceBootloader();
        if (now - ledsAt >= 10) {
            ledsAt = now;
            ShowLeds(now, shown);
        }
        if (!store.Active())
            System::Delay(1);
        // Inspect maximumCycles / overruns in a debugger for on-board CPU evidence.
    }
    Halt();
}
