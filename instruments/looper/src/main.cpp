// SPDX-License-Identifier: MIT
// Looper for the Daisy Seed3 Desktop Dev Kit, libDaisy commit in the repository
// README. Engine with loop modes and Slice/Skip, loop persistence and WAV import
// on the microSD card; no effects. USB-MIDI on J7 so far only for the bootloader request.
#include "card_volume.h"
#include "loop_store.h"
#include "looper_engine.h"
#include "version.h"
const char looper_firmware_version[] = "LOOPER-VERSION=" LOOPER_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
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
// USB-MIDI on J7. Notes and CCs are ignored until the looper gets MIDI control.
static MidiUsbHandler usbMidi;

// microSD: loop.wav is saved from the main loop and recalled at power-up;
// S8 imports the next WAV file of 0:/looper/loops. The transfer buffer must be
// reachable by the SDMMC IDMA, which cannot access DTCM.
alignas(32) static uint8_t DSY_SDRAM_BSS transfer[32768];
// Filter phases for importing 44.1 kHz files (40 KB; DTCM has no room for it).
static float DSY_SDRAM_BSS resampleTable[looper::resample::kTableSize];
static looper::LoopStore store;
static bool storageReady = false;
static std::atomic<bool> cardPresent{false}, importRequested{false};

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
    engine.SetVarispeed(VarispeedFromKnob(hw.pot[knobs::Varispeed].Value()));
    engine.SetInertia(hw.pot[knobs::Inertia].Value());
    engine.SetInertialSlope(Settled(knobs::Slope));
    engine.SetStart(Settled(knobs::Start));
    engine.SetSize(Settled(knobs::Size));
    engine.SetSlice(Settled(knobs::Slice));
    engine.SetSkip(Settled(knobs::Skip));
    engine.SetFrippertronicsDecay(Settled(knobs::Decay));

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
    for (size_t i = 0; i < n; ++i) {
        out[0][i] = CodecLevel(in[0][i] + out[0][i]);
        out[1][i] = CodecLevel(inRight[i] + out[1][i]);
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

// Main loop. A SysEx request is answered at once, because a card write may hold
// the reset back for longer than an editor waits for the reply.
static void ServiceUsbMidi() {
    usbMidi.Listen();  // restarts reception after an overflow
    while (usbMidi.HasEvents()) {
        if (!bootloader::IsRequest(usbMidi.PopEvent()))
            continue;
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

static void Halt() {
    while (true)
        System::Delay(100);
}

int main() {
    asm volatile("" : : "r"(looper_firmware_version));  // Keep the version in the BIN.
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(48);
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
    while (true) {
        const uint32_t now = System::GetNow();
        ServiceStorage(now);
        ServiceUsbMidi();
        ServiceBootloader();
        if (now - ledsAt >= 10) {
            ledsAt = now;
            ShowLeds(now, shown);
        }
        if (!store.Active())
            System::Delay(1);
        // Inspect maximumCycles / overruns in a debugger for on-board CPU evidence.
    }
}
