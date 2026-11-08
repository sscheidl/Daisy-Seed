// SPDX-License-Identifier: MIT
// SC Reverb FX firmware for the Seed3 Desktop Dev Kit: stereo line input -> SC Reverb
// (reverb.h, in SDRAM) -> host feed and dry/wet (wet_host.h) -> output. The callback owns
// controls and DSP; main owns MIDI, LEDs and the bootloader.
#include "version.h"
const char sc_reverb_fx_firmware_version[] = "SC-REVERB-FX-VERSION=" SC_REVERB_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "../../../common/audio/wet_host.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "sc_reverb/reverb.h"
#include "step_seq/controls.h"
#include "step_seq/diagnostics.h"
#include "usbd_core.h"
#include <atomic>
#include <cmath>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
constexpr std::size_t kBlockSize = 48;
constexpr uint32_t kLedPeriodMs = 10;
constexpr int kLedFeed = 0, kLedOverrun = 15;
} // namespace fw

static desktop_devkit::Hardware hw;
static DSY_SDRAM_BSS ::seed::fx::ScReverb::Storage reverb_memory; // about 396 KB, never in DTCM
static ::seed::fx::ScReverb reverb;
static ::seed::audio::WetHost<::seed::fx::ScReverb, fw::kBlockSize> host(reverb);
static step_seq::PotFilter pot_filter[8];
static step_seq::CallbackMeter meter;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[3] = {};
static bool pots_started = false;
static uint32_t control_ms = 0;
static std::atomic<uint32_t> led_state{0};

// ----- audio callback ---------------------------------------------------------------------
static void Controls() {
    hw.UpdateAllControls();
    uint16_t keys = 0;
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            keys |= uint16_t(1u << i);
    if(boot_keys.Process(keys, control_ms))
        boot_request.Set(bootloader::Request::kKeys);
    float v[8];
    for(int i = 0; i < 8; ++i)
        v[i] = pot_filter[i].Process(hw.pot[i].Value());
    const bool first = !pots_started;
    pots_started = true;
    const auto moved = [&](int i) { return first || v[i] != last_pot[i]; };
    auto p = reverb.Requested();
    if(moved(0))
        p.feedback = v[0];
    if(moved(1))
        p.lp_hz = ::seed::fx::screverb::kMinLpHz * std::pow(200.f, v[1]); // 100 Hz .. 20 kHz
    if(moved(0) || moved(1))
        reverb.SetParameters(p);
    if(moved(2))
        host.SetMix(v[2]);
    for(int i = 0; i < 3; ++i)
        last_pot[i] = v[i];
    host.SetEnabled(hw.tog_sw17.Pressed());
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    meter.Begin(DWT->CYCCNT);
    if(n != fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
        return;
    }
    ++control_ms;
    Controls();
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    host.Process(in[0], right, out[0], out[1], n);
    for(size_t i = 0; i < n; ++i) { // output protection belongs to the host
        out[0][i] = out[0][i] > 1.f ? 1.f : (out[0][i] < -1.f ? -1.f : out[0][i]);
        out[1][i] = out[1][i] > 1.f ? 1.f : (out[1][i] < -1.f ? -1.f : out[1][i]);
    }
    meter.End(DWT->CYCCNT);
    uint32_t lit = 0;
    if(host.Enabled())
        lit |= 1u << fw::kLedFeed;
    if(meter.Overruns())
        lit |= 1u << fw::kLedOverrun;
    led_state.store(lit, std::memory_order_relaxed);
}

// ----- main loop --------------------------------------------------------------------------
namespace fw {
static void SendUsb(uint8_t* bytes, std::size_t size) {
    if(hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)
        usb_midi.SendMessage(bytes, size);
}

static void ServiceMainLoop() {
    usb_midi.Listen();
    while(usb_midi.HasEvents()) {
        MidiEvent e = usb_midi.PopEvent();
        if(e.type == SystemCommon && e.sc_type == SystemExclusive && bootloader::IsRequest(e))
            boot_request.Set(bootloader::Request::kSysEx);
    }
    hw.midi.Listen();
    while(hw.midi.HasEvents())
        hw.midi.PopEvent();
    static uint32_t last_led = 0, shown = ~0u;
    const uint32_t now = System::GetNow();
    if(now - last_led >= kLedPeriodMs) {
        last_led = now;
        const uint32_t lit = led_state.load(std::memory_order_relaxed);
        if(lit != shown) {
            for(int i = 0; i < 16; ++i)
                hw.SetLed(i, (lit >> i) & 1 ? 0.6f : 0.f);
            hw.UpdateLeds();
            shown = lit;
        }
    }
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) { SendUsb(bytes, size); });
}
} // namespace fw

static bool Initialize() {
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(fw::kBlockSize);
    for(auto& p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    // SDRAM is up only after hw.Init().
    if(!reverb.Prepare({48000, fw::kBlockSize}, reverb_memory))
        return false;
    host.Reset(true, 0.f);
    meter.Init((SystemCoreClock / 48000u) * uint32_t(fw::kBlockSize));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.midi.StartReceive();
    return true;
}

static void ServiceMain() { fw::ServiceMainLoop(); }

int main() {
    asm volatile("" : : "r"(sc_reverb_fx_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
