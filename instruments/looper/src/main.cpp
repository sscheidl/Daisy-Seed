// SPDX-License-Identifier: MIT
// Looper for the Daisy Seed3 Desktop Dev Kit, libDaisy commit in the repository
// README. Engine with loop modes and Slice/Skip; no SD card, MIDI or effects.
#include "looper_engine.h"
#include "version.h"
const char looper_firmware_version[] = "LOOPER-VERSION=" LOOPER_VERSION;
#include "desktop_devkit.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
using namespace daisy;

#ifndef LOOPER_BUFFER_FRAMES
#define LOOPER_BUFFER_FRAMES looper::kMaxFrames
#endif
static_assert(LOOPER_BUFFER_FRAMES <= looper::kMaxFrames, "buffer exceeds the engine limit");

namespace keys {
enum : unsigned { Record = 0, Play = 1, Undo = 2, Reverse = 3, Erase = 4, LoopMode = 5, Seam = 6 };
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
    const float* inRight = hw.right_detect.Pressed() ? in[1] : in[0];
    engine.Process(in[0], inRight, out[0], out[1], n);  // loop playback only
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
    budgetCycles = (SystemCoreClock / 48000u) * 48u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.seed.StartAudio(AudioCallback);
    uint32_t shown = ~0u;
    while (true) {
        System::Delay(10);
        const uint32_t s = uiState.load(std::memory_order_relaxed);
        const bool blink = (System::GetNow() / 125u) & 1u;
        const uint32_t frame = s | (blink ? 1u << 31 : 0u) |
                               (overruns.load(std::memory_order_relaxed) ? 1u << 30 : 0u);
        if (frame == shown)
            continue;  // UpdateLeds only on changes
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
        hw.SetLed(15, (frame & (1u << 30)) ? 1.f : 0.f);
        hw.UpdateLeds();
        // Inspect maximumCycles / overruns in a debugger for on-board CPU evidence.
    }
}
