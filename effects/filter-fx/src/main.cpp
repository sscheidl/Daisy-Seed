// SPDX-License-Identifier: MIT
// Filter FX firmware for the Seed3 Desktop Dev Kit: stereo line input -> Filter FX
// (filter_fx.h) -> dry/wet -> 10 ms on/off fade -> output, as drive-fx. Pots, keys and
// switches per README.md (a proposal until the owner confirms it), MIDI CC (omni, USB and
// DIN) and SQ parameters over USB. Callback owns controls and DSP; main owns MIDI, SQ, LEDs
// and the bootloader.
#include "version.h"
const char filter_fx_firmware_version[] = "FILTER-FX-VERSION=" FILTER_FX_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "filter_fx/filter_fx.h"
#include "filter_fx/host.h"
#include "filter_fx/params.h"
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
constexpr unsigned kShapeKey = 9;   // S10-S13 select the LFO shape; S1-S8 the mode
constexpr unsigned kStereoKey = 13; // S14 toggles the stereo LFO; S9 and S16 stay free
constexpr unsigned kEnabledLed = 14, kOverrunLed = 15;
constexpr uint32_t kLedPeriodMs = 10;

struct Command {
    uint16_t id = 0;
    int32_t value = 0;
    uint16_t revision = 0;
    bool from_sq = false; // SQ parameter: `revision` is applied (Codex F10)
};
struct Snapshot {
    ::seed::fx::filter_fx::Parameters params;
    uint16_t applied_revision = 0;
    bool overrun = false;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static ::seed::fx::FilterFx filter;
static ::seed::fx::FilterFx::Storage filter_storage;
static ::seed::fx::FilterDryWet dry_wet;
static ::seed::fx::DriveMixer mixer;
static ::seed::fx::filter_fx::Parameters params;
static step_seq::PotFilter pot_filter[8];
static step_seq::Switch3Debounce sw18;
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 32> commands;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[8] = {};
static bool pots_started = false, last_sw17 = false, overrun_seen = false;
static int sw18_position = -1;
static uint16_t held = 0;
static uint32_t control_ms = 0;
static uint16_t applied_revision = 0;
static float wet_l[fw::kBlockSize], wet_r[fw::kBlockSize];

// ----- audio callback ---------------------------------------------------------------------
static void Controls() {
    namespace ff = ::seed::fx::filter_fx;
    hw.UpdateAllControls();
    uint16_t keys = 0;
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            keys |= uint16_t(1u << i);
    if(boot_keys.Process(keys, control_ms))
        boot_request.Set(bootloader::Request::kKeys);
    const uint16_t rise = keys & ~held;
    held = keys;
    for(unsigned i = 0; i < ::seed::dsp::filter::kModeCount; ++i)
        if(rise & (1u << i))
            params.mode = ff::Mode(i);
    for(unsigned i = 0; i < 4; ++i)
        if(rise & (1u << (fw::kShapeKey + i)))
            params.lfo_shape = ff::LfoShape(i);
    if(rise & (1u << fw::kStereoKey))
        params.lfo_stereo = !params.lfo_stereo;
    // Pots act when they move (MIDI CC and the panel win until then).
    float v[8];
    for(int i = 0; i < 8; ++i)
        v[i] = pot_filter[i].Process(hw.pot[i].Value());
    const bool first = !pots_started;
    pots_started = true;
    const auto moved = [&](int i) { return first || v[i] != last_pot[i]; };
    if(moved(0))
        params.cutoff = 24.f + v[0] * 111.f; // linear in notes: 33 Hz .. 19.9 kHz
    if(moved(1))
        params.resonance = v[1] * 100.f;
    if(moved(2))
        params.drive = v[2] * 100.f;
    if(moved(3))
        params.mix = v[3] * 100.f;
    if(moved(4))
        params.lfo_rate = 0.05f * ::seed::dsp::Exp2(v[4] * 8.64385619f); // log2(400)
    if(moved(5))
        params.lfo_depth = v[5] * 48.f;
    if(moved(6))
        params.env_amount = -48.f + v[6] * 96.f;
    if(moved(7))
        params.env_release = 10.f * ::seed::dsp::Exp2(v[7] * 7.64385619f); // log2(200)
    for(int i = 0; i < 8; ++i)
        last_pot[i] = v[i];
    // SW17: on/off when it moves; the enable parameter (CC 20, panel) otherwise.
    const bool sw17 = hw.tog_sw17.Pressed();
    if(first || sw17 != last_sw17)
        params.enabled = sw17;
    last_sw17 = sw17;
    // SW18 up/middle/down: bass compensation full, half, off, when it moves.
    const int sw = hw.tog_sw18.Read();
    const int position = sw18.Process(sw == Switch3::POS_UP ? 2 : (sw == Switch3::POS_DOWN ? 0 : 1), control_ms);
    if(position != sw18_position)
        params.bass_comp = ff::BassComp(position);
    sw18_position = position;
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
    while(commands.Pop(c)) {
        ::seed::fx::filter_fx::WriteParam(params, c.id, c.value);
        if(c.from_sq)
            applied_revision = c.revision;
    }
    Controls();
    filter.SetParameters(params);
    dry_wet.Set(params.mix * 0.01f);
    mixer.SetEnabled(params.enabled);
    // A mono source in the left jack feeds both channels (as the other effect firmwares).
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    if(!filter.Process({in[0], right, wet_l, wet_r, n}))
        for(size_t i = 0; i < n; ++i)
            wet_l[i] = wet_r[i] = 0.f;
    dry_wet.Process(in[0], right, wet_l, wet_r, n);
    mixer.Process(in[0], right, wet_l, wet_r, out[0], out[1], n);
    for(size_t i = 0; i < n; ++i) { // output protection belongs to the host
        out[0][i] = out[0][i] > 1.f ? 1.f : (out[0][i] < -1.f ? -1.f : out[0][i]);
        out[1][i] = out[1][i] > 1.f ? 1.f : (out[1][i] < -1.f ? -1.f : out[1][i]);
    }
    fw::Snapshot& s = snapshots.Back();
    s.params = params;
    s.applied_revision = applied_revision;
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
static int32_t values[sq::kFilterFxComponent.count];
static sq::Component components[] = {{"filter-fx", sq::KindEffect, FILTER_FX_VERSION, &sq::kFilterFxComponent, values}};
class Host : public sq::Host {
  public:
    bool PushParam(uint8_t, uint16_t id, int32_t value, uint16_t revision) override {
        return commands.Push({id, value, revision, true});
    }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override { SendUsb(bytes, size); }
};
static Host host;
static sq::Device device(host);
static ::seed::fx::filter_fx::Parameters last_params;

static void HandleEvent(MidiEvent e, bool usb) {
    if(e.type == ControlChange) {
        for(uint8_t i = 0; i < sq::kFilterFxComponent.count; ++i) {
            const sq::ParamDesc& d = sq::kFilterFxComponent.params[i];
            if(d.cc != e.data[0])
                continue;
            const double lo = d.min / 1000.0, hi = d.max / 1000.0, x = e.data[1] / 127.0;
            const double v = d.type == sq::TypeBool   ? (e.data[1] >= 64 ? 1 : 0)
                             : d.type == sq::TypeEnum ? std::floor(e.data[1] * double(d.enum_count) / 128.0)
                             : (d.flags & sq::FlagLog) ? lo * std::pow(hi / lo, x)
                                                       : lo + (hi - lo) * x;
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
        for(uint8_t i = 0; i < sq::kFilterFxComponent.count; ++i) {
            const uint16_t id = sq::kFilterFxComponent.params[i].id;
            int32_t value = 0, old = 0;
            ::seed::fx::filter_fx::ReadParam(s.params, id, value);
            ::seed::fx::filter_fx::ReadParam(last_params, id, old);
            values[i] = value;
            if(value != old)
                device.ParamChanged(0, id);
        }
        last_params = s.params;
        device.view.applied_revision = s.applied_revision;
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
        uint32_t lit = 1u << unsigned(s.params.mode);
        lit |= 1u << (kShapeKey + unsigned(s.params.lfo_shape));
        if(s.params.lfo_stereo)
            lit |= 1u << kStereoKey;
        if(s.params.enabled)
            lit |= 1u << kEnabledLed;
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
    if(!filter.Prepare({48000, fw::kBlockSize}, filter_storage))
        return false;
    dry_wet.Reset(params.mix * 0.01f);
    mixer.Reset(true);
    Random::Init();
    fw::device.uid[0] = HAL_GetUIDw0();
    fw::device.uid[1] = HAL_GetUIDw1();
    fw::device.uid[2] = HAL_GetUIDw2();
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "filter-fx";
    fw::device.version = FILTER_FX_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapDin | sq::CapMonitor;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    fw::last_params = params;
    for(uint8_t i = 0; i < sq::kFilterFxComponent.count; ++i)
        ::seed::fx::filter_fx::ReadParam(params, sq::kFilterFxComponent.params[i].id, fw::values[i]);
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
    asm volatile("" : : "r"(filter_fx_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
