// SPDX-License-Identifier: MIT
// Standalone Streichwurst FX firmware for the Seed3 Desktop Dev Kit ("Modular FX first",
// AGENTS.md): the FX component is accepted on its own before the Streichwurst product.
//
// Stereo line input (a mono source in the left jack feeds both sides) -> mono feed of the
// string stage (Animate forming, Ensemble common sum, Phaser, stereo tap correction) ->
// global reverb stage -> host mix (ensemble gain word, input/output level, output
// protection). The same FX code and parameter contract as the Streichwurst product; this
// host owns only the board mapping, routing and mixing. With the ensemble off and Animate
// and Phaser at 0 the string stage passes the input exactly.
//
// Pots act when they move (MIDI CC and the panel win until then): VR1 Animate, VR2
// Animate rate, VR3 Phaser, VR4 Phaser rate, VR5 Reverb, VR6 Reverb decay, VR7 input
// level, VR8 output level. Keys: S1-S3 ensemble mode 0/1/2, S12 ensemble on/off, S16
// clears line and reverb (on release, not during the S9+S16 bootloader chord).
// MIDI CC on any channel (USB and DIN), SQ parameters over USB. Callback owns controls
// and DSP; main owns MIDI, SQ, LEDs and the bootloader.
#include "version.h"
#include "streichwurst_fx/version.h"
const char streichwurst_fx_firmware_version[] = "STREICHWURST-FX-VERSION=" STREICHWURST_FX_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "register.h"
#include "step_seq/controls.h"
#include "step_seq/device.h"
#include "step_seq/diagnostics.h"
#include "step_seq/spsc.h"
#include "streichwurst_fx/fx.h"
#include <atomic>
#include <cmath>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
namespace sw = streichwurst::product;
constexpr std::size_t kBlockSize = 48;
constexpr unsigned kOverrunLed = 15;
constexpr uint32_t kLedPeriodMs = 10;
constexpr uint16_t kS9 = 1u << 8, kS16 = 1u << 15;

struct Command {
    uint16_t id = 0;
    int32_t value = 0;
    uint16_t revision = 0;
    bool from_sq = false; // SQ parameter: `revision` is applied
};
struct Snapshot {
    streichwurst_fx::FxParams params;
    uint16_t applied_revision = 0;
    bool overrun = false;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static streichwurst_fx::StreichwurstFx fx;
static DSY_SDRAM_BSS streichwurst_fx::FxStorage fx_storage; // reverb lines, outside DTCM
static fw::sw::State state;                                 // only the FX part is used
static step_seq::PotFilter pot_filter[8];
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 32> commands;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[8] = {};
static bool pots_started = false, overrun_seen = false, s16_armed = false;
static uint16_t held = 0;
static uint32_t control_ms = 0;
static uint16_t applied_revision = 0;
static float input_gain = 1.f, output_gain = 1.f;
static float mono[fw::kBlockSize], sl[fw::kBlockSize], sr[fw::kBlockSize], gain[fw::kBlockSize];
static float wl[fw::kBlockSize], wr[fw::kBlockSize];

static void SetFx(uint16_t id, float normalized) {
    const sq::ParamDesc* d = sq::FindParam(fw::sw::Table(fw::sw::kFx), id);
    fw::sw::WriteParam(state, fw::sw::kFx, id, fw::sw::FromNormalized(*d, normalized));
}

// ----- audio callback ---------------------------------------------------------------------
static void Controls() {
    hw.UpdateAllControls();
    uint16_t keys = 0;
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            keys |= uint16_t(1u << i);
    if(boot_keys.Process(keys, control_ms))
        boot_request.Set(bootloader::Request::kKeys);
    const uint16_t rise = keys & ~held, fall = held & ~keys;
    held = keys;
    for(unsigned i = 0; i < 3; ++i)
        if(rise & (1u << i))
            state.fx.ensemble_mode = int(i);
    if(rise & (1u << 11))
        state.fx.ensemble_on = !state.fx.ensemble_on;
    if(rise & fw::kS16)
        s16_armed = true;
    if((keys & fw::kS9) && (keys & fw::kS16))
        s16_armed = false; // bootloader chord: no clear
    if((fall & fw::kS16) && s16_armed) {
        s16_armed = false;
        fx.ClearTails();
    }
    float v[8];
    for(int i = 0; i < 8; ++i)
        v[i] = pot_filter[i].Process(hw.pot[i].Value());
    const bool first = !pots_started;
    pots_started = true;
    const auto moved = [&](int i) { return first || v[i] != last_pot[i]; };
    using namespace fw::sw::id;
    static constexpr uint16_t kPotParam[6] = {Animate, AnimateRate, Phaser, PhaserRate, Reverb, ReverbDecay};
    for(int i = 0; i < 6; ++i)
        if(moved(i) && !first) // power-up keeps the contract defaults until a pot moves
            SetFx(kPotParam[i], v[i]);
    if(moved(6))
        input_gain = 2.f * v[6] * v[6]; // own taper, unity at the centre position 0.707
    if(moved(7))
        output_gain = v[7] * v[7];
    for(int i = 0; i < 8; ++i)
        last_pot[i] = v[i];
}

static float Protect(float x) {
    if(!std::isfinite(x))
        return 0.f;
    const float a = std::fabs(x);
    if(a <= 0.8f)
        return x;
    float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
    y = y < 0.99997f ? y : 0.99997f;
    return x < 0.f ? -y : y;
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
        fw::sw::WriteParam(state, fw::sw::kFx, c.id, c.value);
        if(c.from_sq)
            applied_revision = c.revision;
    }
    Controls();
    fx.SetParameters(state.fx);
    const float* right = hw.right_detect.Pressed() ? in[1] : in[0];
    for(size_t i = 0; i < n; ++i)
        mono[i] = 0.5f * (in[0][i] + right[i]) * input_gain;
    fx.ProcessStrings(mono, sl, sr, gain, n);
    fx.ProcessReverb(sl, sr, wl, wr, n);
    const float w = fx.ReverbWet();
    for(size_t i = 0; i < n; ++i) {
        const float g = gain[i] * output_gain;
        out[0][i] = Protect((sl[i] + (wl[i] - sl[i]) * w) * g);
        out[1][i] = Protect((sr[i] + (wr[i] - sr[i]) * w) * g);
    }
    fw::Snapshot& s = snapshots.Back();
    s.params = state.fx;
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
static int32_t values[sq::kStreichwurstFxComponent.count];
static sq::Component components[] = {
    {"streichwurst-fx", sq::KindEffect, STREICHWURST_FX_VERSION, &sq::kStreichwurstFxComponent, values}};
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
static sw::State last;

static void HandleEvent(MidiEvent e, bool usb) {
    if(e.type == ControlChange) {
        const sw::CcTarget t = sw::ParamOfCc(e.data[0]);
        if(t.desc && t.component == sw::kFx)
            commands.Push({t.desc->id, sw::FromCc(*t.desc, e.data[1])});
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
        sw::State now_state;
        now_state.fx = s.params;
        for(uint8_t i = 0; i < sq::kStreichwurstFxComponent.count; ++i) {
            const uint16_t id = sq::kStreichwurstFxComponent.params[i].id;
            int32_t value = 0, old = 0;
            sw::ReadParam(now_state, sw::kFx, id, value);
            sw::ReadParam(last, sw::kFx, id, old);
            values[i] = value;
            if(value != old)
                device.ParamChanged(0, id); // pot, key or CC: visible in the app
        }
        last = now_state;
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
        uint32_t lit = 1u << unsigned(s.params.ensemble_mode);
        if(s.params.ensemble_on)
            lit |= 1u << 11;
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
    if(!fx.Prepare(streichwurst_fx::FxConfig{48000.f, fw::kBlockSize}, fx_storage)) // clears the lines
        return false;
    fx.SetParameters(state.fx);
    fx.Reset();
    Random::Init();
    fw::device.uid[0] = HAL_GetUIDw0();
    fw::device.uid[1] = HAL_GetUIDw1();
    fw::device.uid[2] = HAL_GetUIDw2();
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "streichwurst-fx";
    fw::device.version = STREICHWURST_FX_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapDin | sq::CapMonitor;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    fw::last = state;
    for(uint8_t i = 0; i < sq::kStreichwurstFxComponent.count; ++i)
        fw::sw::ReadParam(state, fw::sw::kFx, sq::kStreichwurstFxComponent.params[i].id, fw::values[i]);
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
    asm volatile("" : : "r"(streichwurst_fx_firmware_version)); // Keep the version in the BIN.
    if(!Initialize())
        while(true)
            System::Delay(100);
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
