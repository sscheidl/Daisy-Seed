// SPDX-License-Identifier: MIT
// Delay FX firmware for the Seed3 Desktop Dev Kit (ARBEITSAUFTRAG Phase 4): stereo line
// input -> Stereo Delay (delay.h, 2 x 12 s in SDRAM) -> host feed, tail and dry/wet
// (host.h) -> output. Tempo from VR5 or, while it runs, from received MIDI clock (USB or
// DIN). MIDI CC (omni) and SQ parameters over USB. Callback owns controls and DSP; main
// owns MIDI, SQ, LEDs and the bootloader.
#include "version.h"
const char delay_fx_firmware_version[] = "DELAY-FX-VERSION=" STEREO_DELAY_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "stereo_delay/host.h"
#include "stereo_delay/params.h"
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
constexpr uint32_t kLedPeriodMs = 10, kClockTimeoutMs = 2000;
struct Command {
    enum class Kind : uint8_t { Param, Tempo };
    Kind kind = Kind::Param;
    uint16_t id = 0, revision = 0;
    bool from_sq = false; // SQ parameter: `revision` is applied (Codex F10)
    int32_t value = 0;
    float quarter = 0.f;
};
struct Snapshot {
    ::seed::fx::delay::Parameters params;
    uint16_t applied_revision = 0;
    bool clamped = false, overrun = false, external = false;
    uint8_t state = 0;
    float quarter = 24000.f;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static DSY_SDRAM_BSS ::seed::fx::StereoDelay::Storage delay_memory; // 4.6 MB, never in DTCM
static ::seed::fx::StereoDelay delay;
static ::seed::fx::DelayHost host(delay);
static ::seed::fx::delay::Parameters params;
static step_seq::PotFilter pot_filter[8];
static step_seq::Stepper note_stepper{18};
static step_seq::Switch3Debounce sw18;
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 32> commands;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[8] = {};
static bool pots_started = false, last_sw17 = false, overrun_seen = false, external_tempo = false;
static int last_sw18 = -1;
static float pot_quarter = 24000.f;
static uint32_t control_ms = 0;
static uint16_t applied_revision = 0;

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
    if(moved(0))
        params.time_ms = 10.f * std::pow(200.f, v[0]); // 10 ms .. 2 s, logarithmic
    if(moved(1))
        params.feedback = v[1] * 95.f;
    if(moved(2))
        params.mix = v[2] * 100.f;
    if(moved(3))
        params.damping_hz = 1000.f * std::pow(20.f, v[3]); // 1 .. 20 kHz
    if(moved(4)) {
        pot_quarter = 48000.f * 60.f / (30.f + v[4] * 270.f); // VR5: 30..300 BPM
        external_tempo = false;
    }
    const int note = note_stepper.Update(v[5]);
    if(moved(5))
        params.note_value = note;
    for(int i = 0; i < 8; ++i)
        last_pot[i] = v[i];
    // SW18: up Ping-Pong free, middle Ping-Pong synced, down Stereo L/R synced (when it moves).
    const int raw = hw.tog_sw18.Read();
    const int position = sw18.Process(raw == Switch3::POS_UP ? 0 : (raw == Switch3::POS_DOWN ? 2 : 1), control_ms);
    if(position != last_sw18) {
        params.mode = position == 2 ? ::seed::fx::delay::Mode::Stereo : ::seed::fx::delay::Mode::PingPong;
        params.sync = position != 0;
        last_sw18 = position;
    }
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
    while(commands.Pop(c)) {
        if(c.kind == fw::Command::Kind::Tempo) {
            delay.SetTempo(c.quarter);
            external_tempo = c.quarter > 0.f;
        } else {
            ::seed::fx::delay::WriteParam(params, c.id, c.value);
            if(c.from_sq)
                applied_revision = c.revision;
        }
    }
    Controls();
    if(!external_tempo)
        delay.SetTempo(pot_quarter);
    delay.SetParameters(params);
    host.SetEnabled(params.enabled);
    host.SetMix(params.mix);
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    host.Process(in[0], right, out[0], out[1], n);
    for(size_t i = 0; i < n; ++i) { // output protection belongs to the host
        out[0][i] = out[0][i] > 1.f ? 1.f : (out[0][i] < -1.f ? -1.f : out[0][i]);
        out[1][i] = out[1][i] > 1.f ? 1.f : (out[1][i] < -1.f ? -1.f : out[1][i]);
    }
    fw::Snapshot& s = snapshots.Back();
    s.params = params;
    s.applied_revision = applied_revision;
    s.clamped = delay.Clamped();
    s.overrun = overrun_seen;
    s.external = external_tempo;
    s.state = uint8_t(host.GetState());
    s.quarter = delay.Tempo();
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
static int32_t values[sq::kStereoDelayComponent.count];
static sq::Component components[] = {
    {"stereo-delay", sq::KindEffect, STEREO_DELAY_VERSION, &sq::kStereoDelayComponent, values}};
class Host : public sq::Host {
  public:
    bool PushParam(uint8_t, uint16_t id, int32_t value, uint16_t revision) override {
        Command c;
        c.id = id;
        c.value = value;
        c.revision = revision;
        c.from_sq = true;
        return commands.Push(c);
    }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override { SendUsb(bytes, size); }
};
static Host sq_host;
static sq::Device device(sq_host);
static ::seed::fx::delay::Parameters last_params;
// MIDI clock: tempo from the time of the last 24 clocks (one quarter note).
static uint32_t clock_times[25] = {};
static int clock_count = 0;
static uint32_t last_clock = 0;
static bool clock_running = false;

static void Clock(uint32_t now) {
    for(int i = 24; i > 0; --i)
        clock_times[i] = clock_times[i - 1];
    clock_times[0] = now;
    if(clock_count < 25)
        ++clock_count;
    last_clock = now;
    if(clock_count == 25 && clock_times[0] > clock_times[24]) {
        Command c;
        c.kind = Command::Kind::Tempo;
        c.quarter = float(clock_times[0] - clock_times[24]) * 48.f;
        commands.Push(c);
        clock_running = true;
    }
}

static void HandleEvent(MidiEvent e, bool usb) {
    if(e.type == SystemRealTime && e.srt_type == TimingClock) {
        Clock(System::GetNow());
    } else if(e.type == ControlChange) {
        for(uint8_t i = 0; i < sq::kStereoDelayComponent.count; ++i) {
            const sq::ParamDesc& d = sq::kStereoDelayComponent.params[i];
            if(d.cc != e.data[0])
                continue;
            const double lo = d.min / 1000.0, hi = d.max / 1000.0, x = e.data[1] / 127.0;
            const double v = d.type == sq::TypeBool   ? (e.data[1] >= 64 ? 1 : 0)
                             : d.type == sq::TypeEnum ? std::floor(e.data[1] * double(d.enum_count) / 128.0)
                             : (d.flags & sq::FlagLog) ? lo * std::pow(hi / lo, x)
                                                       : lo + (hi - lo) * x;
            Command c;
            c.id = d.id;
            c.value = sq::Constrain(d, int32_t(std::lround(v * 1000.0)));
            commands.Push(c);
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
    const uint32_t now = System::GetNow();
    if(clock_running && now - last_clock > kClockTimeoutMs) {
        clock_running = false; // the clock stopped: back to VR5
        clock_count = 0;
        Command c;
        c.kind = Command::Kind::Tempo;
        c.quarter = 0.f;
        commands.Push(c);
    }
    if(snapshots.Update()) {
        const Snapshot& s = snapshots.Front();
        for(uint8_t i = 0; i < sq::kStereoDelayComponent.count; ++i) {
            const uint16_t id = sq::kStereoDelayComponent.params[i].id;
            int32_t value = 0, old = 0;
            ::seed::fx::delay::ReadParam(s.params, id, value);
            ::seed::fx::delay::ReadParam(last_params, id, old);
            values[i] = value;
            if(value != old)
                device.ParamChanged(0, id);
        }
        last_params = s.params;
        device.view.applied_revision = s.applied_revision;
        device.view.tempo_milli = int32_t(48000.f * 60.f / s.quarter * 1000.f);
    }
    device.counters[sq::CntCallbackOverruns] = meter.Overruns();
    device.counters[sq::CntCallbackPeak] = meter.Peak();
    device.counters[sq::CntQueueHighWater] = commands.HighWater();
    device.Service(now);
    static uint32_t last_led = 0, shown = ~0u;
    if(now - last_led >= kLedPeriodMs) {
        last_led = now;
        const Snapshot& s = snapshots.Front();
        uint32_t lit = 0;
        if(s.params.sync)
            lit |= 1u << 0;
        if(s.params.mode == ::seed::fx::delay::Mode::Stereo)
            lit |= 1u << 1;
        if(s.clamped)
            lit |= 1u << 2;
        if(s.external)
            lit |= 1u << 3;
        if(s.state != uint8_t(::seed::fx::DelayHost::State::Off))
            lit |= 1u << 7;
        if(s.overrun)
            lit |= 1u << 15;
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
    if(!delay.Prepare({48000, fw::kBlockSize}, delay_memory)) // clears 4.6 MB before the audio starts
        return false;
    host.Reset(true);
    Random::Init();
    fw::device.uid[0] = HAL_GetUIDw0();
    fw::device.uid[1] = HAL_GetUIDw1();
    fw::device.uid[2] = HAL_GetUIDw2();
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "delay-fx";
    fw::device.version = STEREO_DELAY_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapDin | sq::CapMonitor;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    fw::last_params = params;
    for(uint8_t i = 0; i < sq::kStereoDelayComponent.count; ++i)
        ::seed::fx::delay::ReadParam(params, sq::kStereoDelayComponent.params[i].id, fw::values[i]);
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
    asm volatile("" : : "r"(delay_fx_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
