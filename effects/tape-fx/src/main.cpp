// SPDX-License-Identifier: MIT
// Tape FX standalone firmware for the Seed3 Desktop Dev Kit: stereo line input ->
// Tape FX (tape_fx.h) -> stereo output. Hardware-test surface, no storage writes.
// USB-MIDI on J7 is used only for the bootloader request.
#include "tape_fx.h"
#include "tape_mixer.h"
#include "tape_panel.h"
#include "version.h"
const char tape_fx_firmware_version[] = "TAPE-FX-VERSION=" TAPE_FX_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include <array>
#include <atomic>
using namespace daisy;
// libDaisy's USB device state for the external port (J7 on the carrier).
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace tape_fw {
constexpr std::size_t kBlockSize = 48; // 1 kHz control rate, as Toy Piano
// Keys S1..S6 (index 0..5): MixForge options. S8 shows "Tape on", S16 a CPU overrun.
constexpr unsigned kCurveKeys = 4, kCompressorKey = 4, kBitcrushKey = 5;
constexpr unsigned kTapeLed = 7, kOverrunLed = 15;
// Fixed test values of the optional stages: MixForge's compressor defaults; an
// audible 8-bit, 16 kHz crusher (MixForge's own default of 16 bits is nearly inaudible).
constexpr int kCrushBits = 8, kCrushStride = 3;
enum UiBits : uint32_t { kUiCurveMask = 3u, kUiCompressor = 4u, kUiBitcrush = 8u, kUiBypass = 16u, kUiOverrun = 32u };
} // namespace tape_fw

static desktop_devkit::Hardware hw;
// The 16 KiB delay lines go to the external SDRAM explicitly. Default .bss would be
// DTCM in the BOOT_SRAM layout, which is ~91 % used by the libDaisy firmwares here.
static DSY_SDRAM_BSS ::seed::fx::TapeFx::Storage tape_memory;
static ::seed::fx::TapeFx tape_fx;
static ::seed::fx::TapePanel panel;
static ::seed::fx::TapeMixer mixer;
static ::seed::fx::tape::Parameters options; // MixForge fields; panel fills the TapeVibe ones
static uint32_t down_mask = 0;
static bool overrun_seen = false;
static std::atomic<uint32_t> ui_state{0};
static std::atomic<uint32_t> maximum_cycles{0}, overruns{0};
static uint32_t budget_cycles = 0;
// Software entry into the Daisy bootloader: S9+S16 for 5 s, or SysEx over J7.
static bootloader::KeyCombo boot_keys; // audio callback only
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;

static void InitOptions() {
    options = {};
    options.threshold_db = -18.f; // MixForge THRESH default (-180 = -18.0 dB)
    options.ratio = 4.f;
    options.attack_ms = 10.f;
    options.release_ms = 300.f;
    options.bits = tape_fw::kCrushBits;
    options.crush_stride = tape_fw::kCrushStride;
}

// Audio thread only: controls, parameters and DSP state are never touched elsewhere.
static void Controls() {
    hw.UpdateAllControls();
    uint32_t presses = 0;
    for(unsigned i = 0; i < 16; ++i) {
        const uint32_t bit = 1u << i;
        if(hw.button[i].FallingEdge())
            down_mask &= ~bit;
        if(hw.button[i].RisingEdge() && !(down_mask & bit)) {
            down_mask |= bit;
            presses |= bit;
        }
    }
    if(boot_keys.Process(down_mask, System::GetNow()))
        boot_request.Set(bootloader::Request::kKeys);
    bool changed = false;
    for(unsigned i = 0; i < tape_fw::kCurveKeys; ++i)
        if(presses & (1u << i)) {
            options.curve = ::seed::fx::tape::DriveCurve(i);
            changed = true;
        }
    if(presses & (1u << tape_fw::kCompressorKey)) {
        options.compressor = !options.compressor;
        changed = true;
    }
    if(presses & (1u << tape_fw::kBitcrushKey)) {
        options.bitcrush = !options.bitcrush;
        changed = true;
    }
    std::array<float, ::seed::fx::TapePanel::kNumPots> pots{};
    for(unsigned i = 0; i < pots.size(); ++i)
        pots[i] = hw.pot[i].Value();
    if(panel.Update(pots, hw.tog_sw17.Pressed(), hw.tog_sw18.Read()) || changed)
        tape_fx.SetParameters(panel.Apply(options));
    mixer.SetMix(panel.Mix());
    mixer.SetBypass(panel.Bypass());
    ui_state.store(uint32_t(options.curve) | (options.compressor ? tape_fw::kUiCompressor : 0u) |
                       (options.bitcrush ? tape_fw::kUiBitcrush : 0u) |
                       (panel.Bypass() ? tape_fw::kUiBypass : 0u) | (overrun_seen ? tape_fw::kUiOverrun : 0u),
                   std::memory_order_relaxed);
}

static void RenderLeds(uint32_t ui) {
    for(unsigned i = 0; i < 16; ++i) {
        bool on = false;
        if(i < tape_fw::kCurveKeys)
            on = (ui & tape_fw::kUiCurveMask) == i;
        else if(i == tape_fw::kCompressorKey)
            on = ui & tape_fw::kUiCompressor;
        else if(i == tape_fw::kBitcrushKey)
            on = ui & tape_fw::kUiBitcrush;
        else if(i == tape_fw::kTapeLed)
            on = !(ui & tape_fw::kUiBypass);
        else if(i == tape_fw::kOverrunLed)
            on = ui & tape_fw::kUiOverrun;
        hw.SetLed(i, on ? .5f : 0.f);
    }
    hw.UpdateLeds();
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    const uint32_t start = DWT->CYCCNT;
    if(n != tape_fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
        return;
    }
    Controls();
    // A mono source in the left jack feeds both channels (as the Ambient Reverb firmware).
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    if(!tape_fx.Process({in[0], right, out[0], out[1], n}))
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
    mixer.Process(in[0], right, out[0], out[1], n);
    const uint32_t elapsed = DWT->CYCCNT - start;
    if(elapsed > maximum_cycles.load(std::memory_order_relaxed))
        maximum_cycles.store(elapsed, std::memory_order_relaxed);
    if(elapsed > budget_cycles) {
        overruns.fetch_add(1, std::memory_order_relaxed);
        overrun_seen = true; // S16 LED from the next control update on
    }
}

// Main loop only: the bootloader request; other messages are ignored.
static void ServiceUsbMidi() {
    usb_midi.Listen(); // restarts reception after an overflow
    while(usb_midi.HasEvents())
        if(bootloader::IsRequest(usb_midi.PopEvent()))
            boot_request.Set(bootloader::Request::kSysEx);
}
static void ServiceBootloader() {
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) {
        if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
            usb_midi.SendMessage(bytes, size);
    });
}

static bool Initialize() {
    InitOptions();
    panel.Reset();
    mixer.Reset(panel.Mix(), panel.Bypass());
    tape_fx.SetParameters(panel.Apply(options));
    return tape_fx.Prepare({48000, tape_fw::kBlockSize}, tape_memory);
}

int main() {
    asm volatile("" : : "r"(tape_fx_firmware_version)); // Keep the version in the BIN.
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(tape_fw::kBlockSize);
    for(auto& p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    if(!Initialize())
        while(true)
            System::Delay(100);
    budget_cycles = (SystemCoreClock / 48000u) * uint32_t(tape_fw::kBlockSize);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.seed.StartAudio(AudioCallback);
    uint32_t previous_ui = ~0u;
    while(true) {
        System::Delay(10);
        const uint32_t ui = ui_state.load(std::memory_order_relaxed);
        if(ui != previous_ui) {
            RenderLeds(ui); // I2C LED driver stays out of the audio interrupt
            previous_ui = ui;
        }
        ServiceUsbMidi();
        ServiceBootloader();
        // maximum_cycles / overruns: on-board CPU evidence in a debugger.
    }
}
