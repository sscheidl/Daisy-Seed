// SPDX-License-Identifier: MIT
// Daisy Seed3 Desktop Dev Kit, libDaisy commit in README. No GUI or storage writes.
#include "ambient_reverb.hpp"
#include "version.h"
const char ambient_reverb_firmware_version[] =
    "AMBIENT-REVERB-VERSION=" AMBIENT_REVERB_VERSION;
#include "desktop_devkit.h"
#include <atomic>
#include <cmath>
using namespace daisy;
static desktop_devkit::Hardware hw;
static DSY_SDRAM_BSS ambient::Memory memory;
static ambient::AmbientReverb reverb;
static ambient::Parameters parameters;
static bool held = false, bypassed = false;
static std::atomic<uint32_t> maximumCycles{0}, overruns{0};
static uint32_t budgetCycles = 0;
static std::atomic<uint32_t> uiState{0};
static unsigned controlCountdown = 0, controlInterval = 1;
static float previousKnobs[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint32_t downMask = 0;

// Core owns its parameters in the audio thread. LED/main-loop work never touches DSP state.
static void Controls() {
    hw.UpdateAllControls();
    bool changed = false;
    uint32_t newPresses = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t bit = 1u << i;
        if (hw.button[i].FallingEdge())
            downMask &= ~bit;
        if (hw.button[i].RisingEdge() && !(downMask & bit)) {
            downMask |= bit;
            newPresses |= bit;
        }
    }
    for (unsigned i = 0; i < 5; ++i)
        if (newPresses & (1u << i)) {
            parameters.mode = ambient::Mode(i);
            changed = true;
        }
    if (newPresses & (1u << 5)) {
        held = !held;
        changed = true;
    }
    if (newPresses & (1u << 6)) {
        bypassed = !bypassed;
        changed = true;
    }
    if (newPresses & (1u << 7)) {
        parameters.shape = ambient::Shape((unsigned(parameters.shape) + 1) % 5);
        changed = true;
    }
    if (controlCountdown == 0) {
        controlCountdown = controlInterval;
        float k[8];
        for (unsigned i = 0; i < 8; ++i) {
            k[i] = hw.pot[i].Value();
            if (std::abs(k[i] - previousKnobs[i]) > .002f)
                changed = true;
        }
        if (changed) {
            parameters.decaySeconds = std::pow(45.f, k[0]);
            parameters.toneHz = 400.f * std::pow(35.f, k[1]);
            parameters.modDepthMs = 3.f * k[2];
            parameters.mix = k[3];
            parameters.width = k[4];
            parameters.texture = k[5];
            parameters.riseSeconds = .05f * std::pow(80.f, k[5]);
            parameters.modRateHz = .03f * std::pow(50.f, k[6]);
            parameters.preDelayMs = 150.f * k[7];
            for (unsigned i = 0; i < 8; ++i)
                previousKnobs[i] = k[i];
        }
    }
    --controlCountdown;
    if (changed) {
        parameters.hold = held;
        parameters.bypass = bypassed;
        reverb.SetParameters(parameters);
        uiState.store(unsigned(parameters.mode) | (held ? 8u : 0u) | (bypassed ? 16u : 0u) |
                          (unsigned(parameters.shape) << 5),
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
static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    const uint32_t start = DWT->CYCCNT;
    Controls();
    const bool stereo = hw.right_detect.Pressed();
    for (size_t i = 0; i < n; ++i) {
        auto y = reverb.Process({in[0][i], stereo ? in[1][i] : in[0][i]});
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
        System::Delay(50);
        const uint32_t ui = uiState.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < 5; ++i)
            hw.SetLed(i, (ui & 7u) == i ? .5f : 0.f);
        hw.SetLed(5, (ui & 8u) ? .5f : 0.f);
        hw.SetLed(6, (ui & 16u) ? .5f : 0.f);
        hw.SetLed(7, .1f + .15f * float(ui >> 5));
        hw.SetLed(15, overruns.load(std::memory_order_relaxed) ? 1.f : 0.f);
        hw.UpdateLeds();
        // Inspect maximumCycles / overruns in a debugger for on-board CPU evidence.
    }
}
