// SPDX-License-Identifier: MIT
// Acid 303 firmware for the Seed3 Desktop Dev Kit (ARBEITSAUFTRAG Phase 2, 4.6): the voice
// as a standalone instrument. S1-S16 chromatic from C2, VR1-VR8 for the voice, SW17 accent
// for the keys, SW18 slide mode; MIDI notes and CCs (omni) over USB (J7) and DIN, SQ
// parameters over USB. The callback owns controls and the voice; main owns MIDI, SQ,
// LEDs and the bootloader. Hand-over only through SPSC queues and snapshots.
#include "version.h"
const char acid_303_firmware_version[] = "ACID-303-FW-VERSION=" ACID_303_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "acid303/critical.h"
#include "acid303/params.h"
#include "acid303/voice.h"
#include "step_seq/controls.h"
#include "step_seq/device.h"
#include "step_seq/diagnostics.h"
#include "step_seq/spsc.h"
#include <atomic>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
constexpr std::size_t kBlockSize = 48; // 1 kHz control rate
constexpr uint8_t kLowestKey = 36;     // S1 = C2
constexpr uint32_t kLedPeriodMs = 10;
constexpr uint8_t kAccentThreshold = 101; // MIDI velocity above 100 plays an accent

struct Command {
    enum class Kind : uint8_t { Param, NoteOn, NoteOff, AllOff, Mute };
    Kind kind = Kind::Param;
    uint16_t id = 0, revision = 0;
    bool from_sq = false; // SQ parameter: `revision` is applied (Codex F10)
    int32_t value = 0;
    uint8_t note = 0, velocity = 0;
};
struct Snapshot {
    acid303::Params params;
    uint16_t applied_revision = 0;
    uint16_t keys = 0;
    uint8_t note = 0;
    bool gate = false;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static acid303::Voice voice;
static acid303::Params params;
static step_seq::PotFilter pot_filter[8];
static step_seq::Stepper tune_stepper{25}, wave_stepper{2};
static step_seq::Switch3Debounce sw18;
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 64> commands;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static acid303::CriticalInputs critical; // Note-Off, All-Off, Mute when the queue is full
static std::atomic<uint32_t> mute_ack{0};
static bootloader::KeyCombo boot_keys;
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static float last_pot[8] = {};
static bool pots_started = false;
static uint16_t held = 0;
static uint32_t control_ms = 0, mutes = 0, key_tag = 1u << 24;
static uint16_t applied_revision = 0;
static int sw18_position = -1; // last debounced SW18 position, -1 before the first read

// ----- audio callback ---------------------------------------------------------------------
static void ApplyCommand(const fw::Command& c) {
    using K = fw::Command::Kind;
    switch(c.kind) {
        case K::Param:
            acid303::WriteParam(params, c.id, c.value);
            if(c.from_sq)
                applied_revision = c.revision;
            break;
        case K::NoteOn: {
            const bool accent = c.velocity >= fw::kAccentThreshold;
            voice.NoteOn(c.note, c.velocity, accent, false, acid303::Source::MidiIn, ++key_tag);
            break;
        }
        case K::NoteOff: voice.NoteOffNote(c.note, acid303::Source::MidiIn); break;
        case K::AllOff: voice.AllNotesOff(acid303::Source::MidiIn); break;
        case K::Mute:
            voice.Mute();
            ++mutes;
            break;
    }
}

// Pots write only when they move: MIDI CC and the panel win until then (last change wins).
static void Pots() {
    float v[8];
    for(int i = 0; i < 8; ++i)
        v[i] = pot_filter[i].Process(hw.pot[i].Value());
    const bool first = !pots_started;
    pots_started = true;
    const auto moved = [&](int i) { return first || v[i] != last_pot[i]; };
    if(moved(0))
        params.cutoff = 40.f * acid303::Exp2(v[0] * 8.2288f); // 40 Hz .. 12 kHz, logarithmic
    if(moved(1))
        params.resonance = v[1] * 100.f;
    if(moved(2))
        params.env_mod = v[2] * 100.f;
    if(moved(3))
        params.decay = 30.f * acid303::Exp2(v[3] * 6.6439f); // 30 ms .. 3 s
    if(moved(4))
        params.accent = v[4] * 100.f;
    const int tune = tune_stepper.Update(v[5]) - 12, wave = wave_stepper.Update(v[7]);
    if(moved(5))
        params.tune = tune;
    if(moved(6))
        params.volume = v[6] * 100.f;
    if(moved(7))
        params.waveform = wave;
    for(int i = 0; i < 8; ++i)
        last_pot[i] = v[i];
}

static void Keys() {
    uint16_t keys = 0;
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            keys |= uint16_t(1u << i);
    if(boot_keys.Process(keys, control_ms)) {
        voice.Mute(); // silence before the bootloader (4.1)
        boot_request.Set(bootloader::Request::kKeys);
    }
    const bool accent = hw.tog_sw17.Pressed();
    for(int i = 0; i < 16; ++i) {
        const uint16_t bit = uint16_t(1u << i);
        if((keys & bit) && !(held & bit))
            voice.NoteOn(uint8_t(fw::kLowestKey + i), accent ? 127 : 100, accent, false, acid303::Source::MidiIn,
                         ++key_tag);
        if(!(keys & bit) && (held & bit))
            voice.NoteOffNote(uint8_t(fw::kLowestKey + i), acid303::Source::MidiIn);
    }
    held = keys;
}

static void AudioCallback(AudioHandle::InputBuffer, AudioHandle::OutputBuffer out, size_t n) {
    meter.Begin(DWT->CYCCNT);
    if(n != fw::kBlockSize) {
        for(size_t i = 0; i < n; ++i)
            out[0][i] = out[1][i] = 0.f;
        return;
    }
    ++control_ms;
    hw.UpdateAllControls();
    fw::Command c;
    while(commands.Pop(c))
        ApplyCommand(c);
    if(critical.Apply(voice, acid303::Source::MidiIn))
        ++mutes;
    if(mutes)
        mute_ack.store(mutes, std::memory_order_release);
    Pots();
    const int sw = hw.tog_sw18.Read();
    const int position = sw18.Process(sw == Switch3::POS_UP ? 0 : (sw == Switch3::POS_DOWN ? 2 : 1), control_ms);
    // SW18 sets the slide mode when it moves (and at power-up); an SQ or CC value stays
    // until then, like the pots (Codex F12).
    if(position != sw18_position)
        params.slide_mode = position == 0 ? acid303::SlideMode::Off
                                          : (position == 1 ? acid303::SlideMode::Legato : acid303::SlideMode::Always);
    sw18_position = position;
    voice.SetParams(params);
    Keys();
    for(size_t i = 0; i < n; ++i)
        out[0][i] = out[1][i] = voice.Process(); // mono voice on both outputs and the headphones
    fw::Snapshot& s = snapshots.Back();
    s.params = params;
    s.applied_revision = applied_revision;
    s.keys = held;
    s.note = voice.SoundingNote();
    s.gate = voice.Gate();
    snapshots.Publish();
    meter.End(DWT->CYCCNT);
}

// ----- main loop --------------------------------------------------------------------------
namespace fw {
static bool usb_connected = false;
static void SendUsb(const uint8_t* bytes, std::size_t size) {
    if(usb_connected)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}
static int32_t values[sq::kAcid303Component.count];
static sq::Component components[] = {
    {"acid-303", sq::KindInstrument, ACID_303_VERSION, &sq::kAcid303Component, values},
};
class Host : public sq::Host {
  public:
    bool PushParam(uint8_t, uint16_t id, int32_t value, uint16_t revision) override {
        Command c;
        c.kind = Command::Kind::Param;
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
static Host host;
static sq::Device device(host);
static acid303::Params last_params;

// A full queue drops Note-Ons and CCs (counted); Note-Off, All-Off and Mute take the
// fallback path and are never lost (Codex F01).
static void Push(const Command& c) {
    if(commands.Push(c))
        return;
    ++device.counters[sq::CntMidiQueueOverflow];
    switch(c.kind) {
        case Command::Kind::NoteOff: critical.NoteOff(c.note); break;
        case Command::Kind::AllOff: critical.AllOff(); break;
        case Command::Kind::Mute: critical.Mute(); break;
        default: break;
    }
}

static void HandleEvent(MidiEvent e, bool usb) {
    Command c;
    switch(e.type) {
        case NoteOn:
            c.kind = e.data[1] ? Command::Kind::NoteOn : Command::Kind::NoteOff;
            c.note = e.data[0];
            c.velocity = e.data[1];
            Push(c);
            break;
        case NoteOff:
            c.kind = Command::Kind::NoteOff;
            c.note = e.data[0];
            Push(c);
            break;
        case ControlChange: {
            // CC plan (PARAMETERS.md), omni in this firmware.
            if(const sq::ParamDesc* d = acid303::ParamOfCc(sq::kAcid303Component, e.data[0])) {
                c.kind = Command::Kind::Param;
                c.id = d->id;
                c.value = acid303::FromCc(*d, e.data[1]);
                Push(c);
            }
            break;
        }
        case ChannelMode:
            if(e.cm_type == AllNotesOff || e.cm_type == AllSoundOff) {
                c.kind = Command::Kind::AllOff;
                Push(c);
            }
            break;
        case SystemCommon:
            if(!usb || e.sc_type != SystemExclusive)
                break; // SysEx over DIN is not part of this version (4.4)
            if(bootloader::IsRequest(e)) {
                boot_request.Set(bootloader::Request::kSysEx);
            } else {
                uint8_t frame[sq::kMaxFrame + 16];
                const std::size_t size = sq::FromSysExBody(e.sysex_data, e.sysex_message_len, frame, sizeof(frame));
                if(size)
                    device.Handle(frame, size, System::GetNow());
            }
            break;
        default: break;
    }
}

static void ServiceInput() {
    usb_midi.Listen();
    while(usb_midi.HasEvents())
        HandleEvent(usb_midi.PopEvent(), true);
    hw.midi.Listen();
    while(hw.midi.HasEvents())
        HandleEvent(hw.midi.PopEvent(), false);
}

static void ServiceSnapshot() {
    if(!snapshots.Update())
        return;
    const Snapshot& s = snapshots.Front();
    for(uint8_t i = 0; i < sq::kAcid303Component.count; ++i) {
        const uint16_t id = sq::kAcid303Component.params[i].id;
        int32_t value = 0, old = 0;
        acid303::ReadParam(s.params, id, value);
        acid303::ReadParam(last_params, id, old);
        values[i] = value;
        if(value != old)
            device.ParamChanged(0, id);
    }
    last_params = s.params;
    device.view.gate = s.gate;
    device.view.applied_revision = s.applied_revision;
    device.view.usb_connected = usb_connected;
}

static void ServiceUsbState() {
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    if(connected == usb_connected)
        return;
    usb_connected = connected;
    if(!connected) {
        Command c;
        c.kind = Command::Kind::AllOff; // notes from a vanished host must not hang
        Push(c);
    }
}

static void ServiceLeds(uint32_t now) {
    static uint32_t last = 0;
    static uint32_t shown = ~0u;
    if(now - last < kLedPeriodMs)
        return;
    last = now;
    const Snapshot& s = snapshots.Front();
    uint32_t lit = s.keys;
    if(s.gate && s.note >= kLowestKey && s.note < kLowestKey + 16)
        lit |= 1u << (s.note - kLowestKey);
    if(lit == shown)
        return;
    for(int i = 0; i < 16; ++i)
        hw.SetLed(i, (lit >> i) & 1 ? 0.8f : 0.f);
    hw.UpdateLeds();
    shown = lit;
}

static void ServiceStats() {
    device.counters[sq::CntCallbackOverruns] = meter.Overruns();
    device.counters[sq::CntCallbackPeak] = meter.Peak();
    device.counters[sq::CntQueueHighWater] = commands.HighWater();
}

static void ServiceBootloader() {
    if(!boot_request.Pending())
        return;
    const uint32_t before = mute_ack.load(std::memory_order_acquire);
    Command c;
    c.kind = Command::Kind::Mute;
    Push(c);
    const uint32_t start = System::GetNow();
    while(mute_ack.load(std::memory_order_acquire) == before && System::GetNow() - start < 50)
        System::Delay(1);
    System::Delay(10); // the release fades out
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) { SendUsb(bytes, size); });
}
} // namespace fw

static void Initialize() {
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(fw::kBlockSize);
    for(auto& p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    voice.SetParams(params);
    voice.Prepare();
    Random::Init();
    fw::device.uid[0] = HAL_GetUIDw0();
    fw::device.uid[1] = HAL_GetUIDw1();
    fw::device.uid[2] = HAL_GetUIDw2();
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "acid-303";
    fw::device.version = ACID_303_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapVoice | sq::CapDin | sq::CapMonitor;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    fw::last_params = params;
    for(uint8_t i = 0; i < sq::kAcid303Component.count; ++i)
        acid303::ReadParam(params, sq::kAcid303Component.params[i].id, fw::values[i]);
    meter.Init((SystemCoreClock / 48000u) * uint32_t(fw::kBlockSize));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.midi.StartReceive();
}

static void ServiceMain() {
    const uint32_t now = System::GetNow();
    fw::ServiceUsbState();
    fw::ServiceInput();
    fw::ServiceSnapshot();
    fw::ServiceStats();
    fw::device.Service(now);
    fw::ServiceLeds(now);
    fw::ServiceBootloader();
}

int main() {
    asm volatile("" : : "r"(acid_303_firmware_version)); // Keep the version in the BIN.
    Initialize();
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
