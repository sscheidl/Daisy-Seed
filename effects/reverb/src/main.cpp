// SPDX-License-Identifier: MIT
// Daisy Seed3 Desktop Dev Kit, libDaisy commit in README. No GUI or storage writes.
#include "wet_path.hpp"
#include "version.h"
const char ambient_reverb_firmware_version[] =
    "AMBIENT-REVERB-VERSION=" AMBIENT_REVERB_VERSION;
#include "desktop_devkit.h"
#include <atomic>
#include <cmath>
using namespace daisy;
static desktop_devkit::Hardware hw;
static DSY_SDRAM_BSS ambient::Memory memory;
static ambient::WetPath reverb;
static ambient::Parameters parameters;
static bool sustain = false, bypassed = false, bypassPressFromOff = false;
static uint32_t bypassPressTime = 0;
static std::atomic<uint32_t> maximumCycles{0}, overruns{0};
static uint32_t budgetCycles = 0;
static std::atomic<uint32_t> uiState{0};
static unsigned controlCountdown = 0, controlInterval = 1;
static float knobs[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint32_t downMask = 0;
static int widthPosition = Switch3::POS_CENTER, widthCandidate = Switch3::POS_CENTER;
static unsigned widthStable = 0;
static bool legacyMono = false;
static constexpr ambient::Mode kModeButtons[5] = {
    ambient::Mode::Cloud, ambient::Mode::Halo, ambient::Mode::Scatter,
    ambient::Mode::Abyss, ambient::Mode::Bloom};

// Core owns its parameters in the audio thread. LED/main-loop work never touches DSP state.
static void Controls() {
    hw.UpdateAllControls();
    bool changed = false;
    uint32_t newPresses = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t bit = 1u << i;
        if (hw.button[i].FallingEdge()) {
            downMask &= ~bit;
            if (i == 15 && parameters.mode != ambient::Mode::Cloud && sustain) {
                sustain = false;
                changed = true;
            }
            if (i == 14 && bypassPressFromOff) {
                if (uint32_t(System::GetNow() - bypassPressTime) >= 250u) {
                    bypassed = true;
                    changed = true;
                }
                bypassPressFromOff = false;
            }
        }
        if (hw.button[i].RisingEdge() && !(downMask & bit)) {
            downMask |= bit;
            newPresses |= bit;
        }
    }
    for (unsigned i = 0; i < 5; ++i)
        if (newPresses & (1u << i)) {
            parameters.mode = kModeButtons[i];
            sustain = false; // No hidden sustain latch across mode changes.
            changed = true;
        }
    if (newPresses & (1u << 14)) {
        bypassPressFromOff = bypassed;
        bypassPressTime = System::GetNow();
        bypassed = !bypassed;
        changed = true;
    }
    if (newPresses & (1u << 15)) {
        if (parameters.mode == ambient::Mode::Cloud)
            sustain = !sustain;
        else
            sustain = true;
        changed = true;
    }
    const int widthRead = hw.tog_sw18.Read();
    if (widthRead == widthCandidate) {
        if (widthStable < 15)
            ++widthStable;
    } else {
        widthCandidate = widthRead;
        widthStable = 1;
    }
    if (widthStable == 15 && widthPosition != widthCandidate) {
        widthPosition = widthCandidate;
        changed = true;
    }
    const bool newMono = hw.tog_sw17.Pressed();
    if (newMono != legacyMono) {
        legacyMono = newMono;
        reverb.SetLegacyMono(legacyMono);
    }
    if (controlCountdown == 0) {
        controlCountdown = controlInterval;
        for (unsigned i = 0; i < 8; ++i) {
            const float value = hw.pot[i].Value();
            if (std::abs(value - knobs[i]) > .002f) {
                knobs[i] = value;
                changed = true;
            }
        }
        reverb.ServiceFilter();
    }
    --controlCountdown;
    if (changed) {
        // Fixed physical pot positions always win; only selected mode/switch states persist.
        parameters.decaySeconds = std::pow(45.f, knobs[0]);
        parameters.toneHz = 400.f * std::pow(35.f, knobs[1]);
        parameters.modDepthMs = 3.f * knobs[2];
        parameters.mix = knobs[3];
        parameters.pan = 2.f * knobs[4] - 1.f;
        parameters.texture = knobs[5];
        parameters.riseSeconds = .05f * std::pow(80.f, knobs[5]);
        parameters.modRateHz = .03f * std::pow(50.f, knobs[6]);
        parameters.stretch = knobs[7];
        parameters.width = widthPosition == Switch3::POS_UP
                               ? 1.f
                               : (widthPosition == Switch3::POS_CENTER ? .5f : 0.f);
        parameters.sustain = sustain;
        parameters.bypass = bypassed;
        reverb.SetParameters(parameters);
        unsigned selected = 0;
        for (unsigned i = 0; i < 5; ++i)
            if (parameters.mode == kModeButtons[i])
                selected = i;
        uiState.store(selected | (sustain ? 8u : 0u) | (bypassed ? 16u : 0u),
                      std::memory_order_relaxed);
    }
}
static float CodecLevel(float x) {
    // Transparent below -0.45dBFS, soft knee above. Protect final codec conversion.
    const float a = std::abs(x);
    if (a <= .95f)
        return x;
    const float e = a - .95f, y = .95f + .05f * e / (.05f + e);
    return std::copysign(y, x);
}
static void RenderLeds(uint32_t ui) {
    for (unsigned i = 0; i < 14; ++i)
        hw.SetLed(i, i < 5 && (ui & 7u) == i ? .5f : 0.f);
    hw.SetLed(14, (ui & 16u) ? .5f : 0.f); // Bypass is active.
    hw.SetLed(15, (ui & 8u) ? .5f : 0.f);  // Sustain is active.
    hw.UpdateLeds();
}
static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    const uint32_t start = DWT->CYCCNT;
    Controls();
    const bool stereo = hw.right_detect.Pressed();
    for (size_t i = 0; i < n; ++i) {
        auto y = reverb.Process({in[0][i], in[1][i]}, stereo);
        out[0][i] = CodecLevel(y.left);
        out[1][i] = CodecLevel(y.right);
    }
    uint32_t elapsed = DWT->CYCCNT - start;
    if (elapsed > maximumCycles.load(std::memory_order_relaxed))
        maximumCycles.store(elapsed, std::memory_order_relaxed);
    if (elapsed > budgetCycles)
        overruns.fetch_add(1, std::memory_order_relaxed);
}
int main() {
    asm volatile("" : : "r"(ambient_reverb_firmware_version)); // Keep the version in the BIN.
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(32);
    for (auto &p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    parameters = ambient::AmbientReverb::Preset(ambient::Mode::Cloud);
    // The pedal-style control revision starts with a fixed Sine shape and Trails On.
    parameters.shape = ambient::Shape::Sine;
    parameters.trails = true;
    reverb.SetParameters(parameters);
    if (!reverb.Prepare(hw.seed.AudioSampleRate(), memory))
        while (true) {
            System::Delay(100);
        }
    controlInterval = unsigned(hw.seed.AudioCallbackRate() / 50.f);
    if (!controlInterval)
        controlInterval = 1;
    budgetCycles = (SystemCoreClock / 48000u) * 32u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.seed.StartAudio(AudioCallback);
    while (true) {
        // Controls are serviced in the callback; no cross-thread DSP parameter writes.
        System::Delay(10);
        const uint32_t ui = uiState.load(std::memory_order_relaxed);
        static uint32_t previousUi = ~0u;
        if (ui != previousUi) {
            RenderLeds(ui);
            previousUi = ui;
        }
        // Inspect maximumCycles / overruns in a debugger for on-board CPU evidence.
    }
}
