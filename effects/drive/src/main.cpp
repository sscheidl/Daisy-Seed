// SPDX-License-Identifier: MIT
// Drive FX firmware for the Seed3 Desktop Dev Kit (ARBEITSAUFTRAG Phase 3b): stereo line
// input -> Drive (drive.h) -> 10 ms on/off fade (host.h) -> output, as reverb-fx and
// tape-fx. Pots for the drive, keys for curve and auto level, MIDI CC (omni, USB and DIN)
// and SQ parameters over USB. Callback owns controls and DSP; main owns MIDI, SQ, LEDs and
// the bootloader.
#include "version.h"
const char drive_fx_firmware_version[] = "DRIVE-FX-VERSION=" DRIVE_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "drive/drive.h"
#include "drive/host.h"
#include "drive/params.h"
#include "step_seq/controls.h"
#include "step_seq/device.h"
#include "step_seq/diagnostics.h"
#include "step_seq/spsc.h"
#include <atomic>
#include <cmath>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
constexpr std::size_t kBlockSize = 48;
constexpr unsigned kAutoKey = 4;    // S5 toggles auto level; S1-S4 select the curve
constexpr unsigned kOverrunLed = 15;
constexpr uint32_t kLedPeriodMs = 10;

struct Command {
    uint16_t id = 0;
    int32_t value = 0;
};
struct Snapshot {
    ::seed::fx::drive::Parameters params;
    bool overrun = false;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static ::seed::fx::Drive drive;
static ::seed::fx::Drive::Storage drive_storage;
static ::seed::fx::DriveMixer mixer;
static ::seed::fx::drive::Parameters params;
static step_seq::PotFilter pot_filter[8];
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 32> commands;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[8] = {};
static bool pots_started = false, last_sw17 = false, overrun_seen = false;
static uint16_t held = 0;
static uint32_t control_ms = 0;
static float wet_l[fw::kBlockSize], wet_r[fw::kBlockSize];

// ----- audio callback ---------------------------------------------------------------------
static void Controls() {
    hw.UpdateAllControls();
    uint16_t keys = 0;
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            keys |= uint16_t(1u << i);
    if(boot_keys.Process(keys, control_ms))
        boot_request.Set(bootloader::Request::kKeys);
    const uint16_t rise = keys & ~held;
    held = keys;
    for(unsigned i = 0; i < 4; ++i)
        if(rise & (1u << i))
            params.curve = ::seed::fx::drive::Curve(i);
    if(rise & (1u << fw::kAutoKey))
        params.auto_level = !params.auto_level;
    // Pots act when they move (MIDI CC and the panel win until then).
    float v[8];
    for(int i = 0; i < 8; ++i)
        v[i] = pot_filter[i].Process(hw.pot[i].Value());
    const bool first = !pots_started;
    pots_started = true;
    const auto moved = [&](int i) { return first || v[i] != last_pot[i]; };
    if(moved(0))
        params.drive = v[0] * 100.f;
    if(moved(1))
        params.tone = v[1] * 100.f;
    if(moved(2))
        params.level_db = -24.f + v[2] * 36.f;
    if(moved(3))
        params.compensation = v[3] * 100.f;
    if(moved(4))
        params.target_offset_db = -3.f + v[4] * 3.f;
    for(int i = 0; i < 8; ++i)
        last_pot[i] = v[i];
    // SW17: drive on/off when it moves; the enable parameter (CC 20, panel) otherwise.
    const bool sw17 = hw.tog_sw17.Pressed();
    if(first || sw17 != last_sw17)
        params.enabled = sw17;
    last_sw17 = sw17;
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t n) {
    meter.Begin(DWT->CYCCNT);
    if(n != fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
        return;
    }
    ++control_ms;
    fw::Command c;
    while(commands.Pop(c))
        ::seed::fx::drive::WriteParam(params, c.id, c.value);
    Controls();
    drive.SetParameters(params);
    mixer.SetEnabled(params.enabled);
    // A mono source in the left jack feeds both channels (as the other effect firmwares).
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    if(!drive.Process({in[0], right, wet_l, wet_r, n}))
        for(size_t i = 0; i < n; ++i)
            wet_l[i] = wet_r[i] = 0.f;
    mixer.Process(in[0], right, wet_l, wet_r, out[0], out[1], n);
    for(size_t i = 0; i < n; ++i) { // output protection belongs to the host
        out[0][i] = out[0][i] > 1.f ? 1.f : (out[0][i] < -1.f ? -1.f : out[0][i]);
        out[1][i] = out[1][i] > 1.f ? 1.f : (out[1][i] < -1.f ? -1.f : out[1][i]);
    }
    fw::Snapshot& s = snapshots.Back();
    s.params = params;
    s.overrun = overrun_seen;
    snapshots.Publish();
    meter.End(DWT->CYCCNT);
    if(meter.Overruns())
        overrun_seen = true;
}

// ----- main loop --------------------------------------------------------------------------
namespace fw {
static bool usb_connected = false;
static void SendUsb(const uint8_t* bytes, std::size_t size) {
    if(usb_connected)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}
static int32_t values[sq::kDriveComponent.count];
static sq::Component components[] = {{"drive", sq::KindEffect, DRIVE_VERSION, &sq::kDriveComponent, values}};
class Host : public sq::Host {
  public:
    bool PushParam(uint8_t, uint16_t id, int32_t value, uint16_t) override { return commands.Push({id, value}); }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override { SendUsb(bytes, size); }
};
static Host host;
static sq::Device device(host);
static ::seed::fx::drive::Parameters last_params;

static void HandleEvent(MidiEvent e, bool usb) {
    if(e.type == ControlChange) {
        for(uint8_t i = 0; i < sq::kDriveComponent.count; ++i) {
            const sq::ParamDesc& d = sq::kDriveComponent.params[i];
            if(d.cc != e.data[0])
                continue;
            const double lo = d.min / 1000.0, hi = d.max / 1000.0;
            double v = d.type == sq::TypeBool   ? (e.data[1] >= 64 ? 1 : 0)
                       : d.type == sq::TypeEnum ? std::floor(e.data[1] * double(d.enum_count) / 128.0)
                                                : lo + (hi - lo) * e.data[1] / 127.0;
            commands.Push({d.id, sq::Constrain(d, int32_t(std::lround(v * 1000.0)))});
        }
    } else if(usb && e.type == SystemCommon && e.sc_type == SystemExclusive) {
        if(bootloader::IsRequest(e)) {
            boot_request.Set(bootloader::Request::kSysEx);
            return;
        }
        uint8_t frame[sq::kMaxFrame + 16];
        const std::size_t size = sq::FromSysExBody(e.sysex_data, e.sysex_message_len, frame, sizeof(frame));
        if(size)
            device.Handle(frame, size, System::GetNow());
    }
}

static void ServiceMainLoop() {
    usb_connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    usb_midi.Listen();
    while(usb_midi.HasEvents())
        HandleEvent(usb_midi.PopEvent(), true);
    hw.midi.Listen();
    while(hw.midi.HasEvents())
        HandleEvent(hw.midi.PopEvent(), false);
    if(snapshots.Update()) {
        const Snapshot& s = snapshots.Front();
        for(uint8_t i = 0; i < sq::kDriveComponent.count; ++i) {
            const uint16_t id = sq::kDriveComponent.params[i].id;
            int32_t value = 0, old = 0;
            ::seed::fx::drive::ReadParam(s.params, id, value);
            ::seed::fx::drive::ReadParam(last_params, id, old);
            values[i] = value;
            if(value != old)
                device.ParamChanged(0, id);
        }
        last_params = s.params;
    }
    device.counters[sq::CntCallbackOverruns] = meter.Overruns();
    device.counters[sq::CntCallbackPeak] = meter.Peak();
    device.counters[sq::CntQueueHighWater] = commands.HighWater();
    const uint32_t now = System::GetNow();
    device.Service(now);
    static uint32_t last_led = 0, shown = ~0u;
    if(now - last_led >= kLedPeriodMs) {
        last_led = now;
        const Snapshot& s = snapshots.Front();
        uint32_t lit = 1u << unsigned(s.params.curve);
        if(s.params.auto_level)
            lit |= 1u << kAutoKey;
        if(s.params.enabled)
            lit |= 1u << 7;
        if(s.overrun)
            lit |= 1u << kOverrunLed;
        if(lit != shown) {
            for(int i = 0; i < 16; ++i)
                hw.SetLed(i, (lit >> i) & 1 ? 0.6f : 0.f);
            hw.UpdateLeds(); // I2C LED driver stays out of the audio interrupt
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
    if(!drive.Prepare({48000, fw::kBlockSize}, drive_storage))
        return false;
    mixer.Reset(true);
    Random::Init();
    fw::device.uid[0] = HAL_GetUIDw0();
    fw::device.uid[1] = HAL_GetUIDw1();
    fw::device.uid[2] = HAL_GetUIDw2();
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "drive-fx";
    fw::device.version = DRIVE_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapDin | sq::CapMonitor;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    fw::last_params = params;
    for(uint8_t i = 0; i < sq::kDriveComponent.count; ++i)
        ::seed::fx::drive::ReadParam(params, sq::kDriveComponent.params[i].id, fw::values[i]);
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
    asm volatile("" : : "r"(drive_fx_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
