// SPDX-License-Identifier: MIT
// Streichwurst firmware for the Seed3 Desktop Dev Kit (docs/CLAUDE_AUFTRAG.md, Phase 2-4):
// the Streichwurst instrument (strings, soli) and the separate Streichwurst FX composed
// by the product graph (product.h), one parameter register for pots/keys, MIDI CC, the
// SQ protocol (control app) and the Mod on Daisy matrix (MD protocol, same USB port).
//
// Audio callback (48 frames = 1 kHz control rate): controls, queued commands and notes,
// matrix tick, DSP. Main loop: USB/DIN MIDI, SQ and MD protocols, LEDs, bootloader entry.
// Hand-over only through SPSC queues, an atomic All-Notes-Off fallback and snapshots.
#include "version.h"
#include "streichwurst/version.h"
#include "streichwurst_fx/version.h"
const char streichwurst_firmware_version[] = "STREICHWURST-FW-VERSION=" STREICHWURST_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "controls.h"
#include "product.h"
#include "register.h"
#include "mod_on_daisy/module.h"
#include "mod_on_daisy_desktop.h"
#include "step_seq/device.h"
#include "step_seq/diagnostics.h"
#include "step_seq/spsc.h"
#include <atomic>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
using namespace streichwurst::product;
constexpr std::size_t kBlockSize = kMaxBlock; // 48 frames at 48 kHz: 1 kHz control rate
constexpr uint32_t kLedPeriodMs = 10;
constexpr uint32_t kPanicSettleMs = 5;

struct Command {
    enum class Kind : uint8_t { Param, Event, Panic };
    Kind kind = Kind::Param;
    uint8_t component = 0;
    uint16_t id = 0, revision = 0;
    bool counts_revision = false;
    int32_t value = 0;
    streichwurst::Event event;
};
struct Snapshot {
    State state;
    uint16_t applied_revision = 0;
    uint32_t invalid_events = 0, dropped_note_ons = 0, non_finite = 0;
    bool muting = false;
};
} // namespace fw

static desktop_devkit::Hardware hw;
static fw::Product<> product;
// Memory the product places outside DTCM: reverb lines and the instrument's key tables
// (touched only by events). DTCM keeps the per-sample DSP state and the stack.
static DSY_SDRAM_BSS streichwurst_fx::FxStorage fx_memory;
static DSY_SDRAM_BSS streichwurst::InstrumentStorage key_memory;
static fw::Controls controls;
static fw::State state; // audio callback only
static uint16_t applied_revision = 0;
static step_seq::SpscQueue<fw::Command, 64> commands;
static std::atomic<uint32_t> all_off_requests{0}; // releases a full queue could not take
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static std::atomic<uint32_t> led_state{0};
static std::atomic<uint32_t> panic_ack{0};
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static step_seq::CallbackMeter meter;
static mod::Module modulation;
static uint32_t control_ms = 0, panic_count = 0, all_off_seen = 0;

static mod::InstrumentInfo MakeModInfo() {
    mod::InstrumentInfo info;
    info.key = "streichwurst";
    info.version = STREICHWURST_VERSION;
    info.targets[fw::TargetRegistration] = {"Registr.", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetAnimate] = {"Animate", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetPhaser] = {"Phaser", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetReverb] = {"Reverb", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetBalance] = {"Balance", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetSoloTone] = {"Solo Tone", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetTremolo] = {"Tremolo", "+-100%", 1.0f, false, false};
    info.targets[fw::TargetPitch] = {"Pitch", "+-1 st", 0.5f, false, false};
    return info;
}
static const mod::InstrumentInfo kModInfo = MakeModInfo();
static mod::adapter::UsbMidiSender mod_sender(usb_midi);

// ----- audio callback ---------------------------------------------------------------------
static void ApplyCommand(const fw::Command& c, streichwurst::Event* events, int& count, int capacity) {
    using K = fw::Command::Kind;
    switch(c.kind) {
        case K::Param:
            if(fw::WriteParam(state, c.component, c.id, c.value))
                controls.Latch(c.component, c.id); // the pot must pick the new value up
            if(c.counts_revision)
                applied_revision = c.revision;
            break;
        case K::Event:
            if(count < capacity)
                events[count++] = c.event;
            break;
        case K::Panic:
            product.Panic();
            ++panic_count;
            break;
    }
}

// Base value of each matrix column (0..1) from the register; Pitch is centred.
static float MatrixBase(int t) {
    using namespace fw;
    auto norm = [](uint8_t component, uint16_t pid) {
        int32_t v = 0;
        ReadParam(state, component, pid, v);
        return Normalized(*sq::FindParam(Table(component), pid), v);
    };
    switch(t) {
        case TargetRegistration: return norm(kInstrument, id::Registration);
        case TargetAnimate: return norm(kFx, id::Animate);
        case TargetPhaser: return norm(kFx, id::Phaser);
        case TargetReverb: return norm(kFx, id::Reverb);
        case TargetBalance: return norm(kProduct, id::Balance);
        case TargetSoloTone: return norm(kInstrument, id::SoloTone);
        case TargetTremolo: return norm(kInstrument, id::SoloTremDepth);
        default: return 0.5f;
    }
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
    constexpr int kEvents = 64;
    streichwurst::Event events[kEvents];
    int count = 0;
    // A release the full queue could not take becomes All Notes Off after everything that
    // was queued before it, once the queue is drained (never before a queued Note-On).
    const uint32_t all_off = all_off_requests.load(std::memory_order_acquire);
    fw::Command c;
    bool drained = false;
    while(count < kEvents - 1) {
        if(!commands.Pop(c)) {
            drained = true;
            break;
        }
        ApplyCommand(c, events, count, kEvents - 1);
    }
    if(drained && all_off != all_off_seen) {
        all_off_seen = all_off;
        events[count++] = streichwurst::AllNotesOff();
    }

    fw::ControlFrame frame;
    for(int i = 0; i < fw::kPots; ++i)
        frame.pots[i] = hw.pot[i].Value();
    for(int i = 0; i < fw::kKeys; ++i)
        if(hw.button[i].Pressed())
            frame.keys |= uint16_t(1u << i);
    frame.ms = control_ms;
    const fw::ControlOutput ctl = controls.Process(frame, state);
    for(int i = 0; i < ctl.event_count && count < kEvents; ++i)
        events[count++] = ctl.events[i];
    if(ctl.panic) {
        product.Panic();
        ++panic_count;
    }
    if(ctl.bootloader) {
        if(count < kEvents)
            events[count++] = streichwurst::AllSoundOff();
        boot_request.Set(bootloader::Request::kKeys);
    }
    if(panic_count)
        panic_ack.store(panic_count, std::memory_order_release);

    // Matrix (1 kHz): offsets only; an empty matrix leaves every base value untouched.
    mod::Engine& engine = modulation.engine;
    mod::adapter::ReadHardware(engine, hw);
    engine.Tick(n);
    float offsets[fw::kMatrixTargets];
    for(int t = 0; t < fw::kMatrixTargets; ++t) {
        const float base = MatrixBase(t);
        offsets[t] = engine.Apply(std::size_t(t), base) - base;
        engine.ReportBase(std::size_t(t), base);
    }
    product.Update(state, offsets);
    product.Render(events, std::size_t(count), out[0], out[1], n);
    controls.SetPanicActive(product.Muting());

    fw::Snapshot& s = snapshots.Back();
    s.state = state;
    s.applied_revision = applied_revision;
    s.invalid_events = product.GetInstrument().Stats().invalid_events;
    s.dropped_note_ons = product.GetInstrument().Stats().dropped_note_ons;
    s.non_finite = product.Stats().non_finite;
    s.muting = product.Muting();
    snapshots.Publish();
    led_state.store(controls.LedFrame(), std::memory_order_relaxed);
    meter.End(DWT->CYCCNT);
}

// ----- main loop: MIDI, SQ and MD protocols, LEDs, bootloader -----------------------------
namespace fw {
static bool usb_connected = false;
static uint32_t input_dropped = 0; // Note-Ons and CCs a full queue dropped (main only)
static uint8_t channel = 0;        // 0 = omni, from the latest snapshot
static void SendUsb(const uint8_t* bytes, std::size_t size) {
    if(usb_connected)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}
static int32_t values[kComponents][32];
static sq::Component components[kComponents] = {
    {"streichwurst-instrument", sq::KindInstrument, STREICHWURST_INSTRUMENT_VERSION,
     &sq::kStreichwurstInstrumentComponent, values[kInstrument]},
    {"streichwurst-fx", sq::KindEffect, STREICHWURST_FX_VERSION, &sq::kStreichwurstFxComponent, values[kFx]},
    {"streichwurst", sq::KindProduct, STREICHWURST_VERSION, &sq::kStreichwurstComponent, values[kProduct]},
};

class Host : public sq::Host {
  public:
    bool PushParam(uint8_t component, uint16_t pid, int32_t value, uint16_t revision) override {
        Command c;
        c.kind = Command::Kind::Param;
        c.component = component;
        c.id = pid;
        c.value = value;
        c.revision = revision;
        c.counts_revision = true;
        return commands.Push(c);
    }
    // Streichwurst has no pattern, transport or CV calibration (capabilities).
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override { SendUsb(bytes, size); }
};
static Host host;
static sq::Device device(host);
static Snapshot last;

static void PushEvent(const streichwurst::Event& e, bool release) {
    Command c;
    c.kind = Command::Kind::Event;
    c.event = e;
    if(commands.Push(c))
        return;
    if(release)
        all_off_requests.fetch_add(1, std::memory_order_release); // never a hanging note
    else
        ++input_dropped;
}
static void RequestPanic() {
    Command c;
    c.kind = Command::Kind::Panic;
    if(!commands.Push(c))
        all_off_requests.fetch_add(1, std::memory_order_release);
}

// Musical MIDI and the CC plan on the selected channel (0 = omni); Mod on Daisy has
// already taken its SysEx, CC1 and aftertouch.
static void Route(const MidiEvent& e) {
    if(channel && e.channel + 1 != channel)
        return;
    const uint8_t ch = uint8_t(e.channel & 15);
    switch(e.type) {
        case NoteOn:
            if(e.data[1])
                PushEvent(streichwurst::NoteOn(ch, e.data[0], e.data[1]), false);
            else
                PushEvent(streichwurst::NoteOff(ch, e.data[0]), true);
            break;
        case NoteOff: PushEvent(streichwurst::NoteOff(ch, e.data[0]), true); break;
        case PitchBend: {
            const int value = int(e.data[0]) | (int(e.data[1]) << 7);
            PushEvent(streichwurst::PitchBend(ch, int16_t(value - 8192)), false);
            break;
        }
        case ControlChange: {
            if(e.data[0] == 120 || e.data[0] == 123) { // as ChannelMode above, other parsers
                if(e.data[0] == 120)
                    RequestPanic();
                else
                    PushEvent(streichwurst::AllNotesOff(), true);
                break;
            }
            if(e.data[0] == 64) {
                PushEvent(streichwurst::Sustain(ch, e.data[1] >= 64), e.data[1] < 64);
                break;
            }
            const CcTarget t = ParamOfCc(e.data[0]);
            if(!t.desc)
                break;
            Command c;
            c.kind = Command::Kind::Param;
            c.component = t.component;
            c.id = t.desc->id;
            c.value = FromCc(*t.desc, e.data[1]);
            if(!commands.Push(c))
                ++input_dropped;
            break;
        }
        case ChannelMode: // libDaisy parses CC120..127 as channel mode messages
            if(e.cm_type == AllSoundOff)
                RequestPanic();
            else if(e.cm_type == AllNotesOff)
                PushEvent(streichwurst::AllNotesOff(), true);
            break;
        default: break;
    }
}

static void ServiceInput() {
    usb_midi.Listen();
    while(usb_midi.HasEvents()) {
        MidiEvent e = usb_midi.PopEvent();
        if(e.type == SystemCommon && e.sc_type == SystemExclusive) {
            if(bootloader::IsRequest(e)) {
                boot_request.Set(bootloader::Request::kSysEx);
                continue;
            }
            uint8_t frame[sq::kMaxFrame + 16];
            const std::size_t n = sq::FromSysExBody(e.sysex_data, e.sysex_message_len, frame, sizeof(frame));
            if(n && device.Handle(frame, n, System::GetNow()))
                continue;
        }
        mod::MidiMessage m; // Mod on Daisy: MD SysEx, CC1, aftertouch; notes are observed
        if(mod::adapter::ToMessage(e, m) && modulation.controller.HandleMidi(m))
            continue;
        Route(e);
    }
    hw.midi.Listen();
    while(hw.midi.HasEvents()) {
        MidiEvent e = hw.midi.PopEvent();
        if(e.type == SystemCommon)
            continue; // SysEx over DIN is not part of this version
        mod::MidiMessage m;
        if(e.type != SystemRealTime && mod::adapter::ToMessage(e, m) && modulation.controller.HandleMidi(m))
            continue;
        Route(e);
    }
}

static void ServiceSnapshot() {
    if(!snapshots.Update())
        return;
    const Snapshot& s = snapshots.Front();
    device.view.applied_revision = s.applied_revision;
    device.view.usb_connected = usb_connected;
    for(uint8_t k = 0; k < kComponents; ++k)
        for(uint8_t i = 0; i < Table(k).count; ++i) {
            const uint16_t pid = Table(k).params[i].id;
            int32_t value = 0, old = 0;
            ReadParam(s.state, k, pid, value);
            ReadParam(last.state, k, pid, old);
            values[k][i] = value;
            if(value != old)
                device.ParamChanged(k, pid); // pot, key, CC or preset: visible in the app
        }
    if(s.state.prod.midi_channel != last.state.prod.midi_channel)
        PushEvent(streichwurst::AllNotesOff(), true); // notes of the old channel must not hang
    channel = uint8_t(s.state.prod.midi_channel);
    device.counters[sq::CntNoteOnDropped] = s.dropped_note_ons;
    last = s;
}

static void ServiceUsbState() {
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    if(connected == usb_connected)
        return;
    usb_connected = connected;
    if(!connected)
        PushEvent(streichwurst::AllNotesOff(), true); // notes from a vanished host must not hang
}

#ifdef STEP_SEQ_HOST_TEST
static void StackPaint() {}
static uint32_t StackFreeBytes() { return 0; }
#else
extern "C" uint32_t _edtcmram_bss;
static uint32_t *stack_low = nullptr, *stack_high = nullptr;
static void StackPaint() {
    uint32_t sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    stack_low = &_edtcmram_bss;
    stack_high = reinterpret_cast<uint32_t*>((sp - 512u) & ~3u);
    if(stack_high > stack_low)
        step_seq::StackProbe::Paint(stack_low, stack_high);
}
static uint32_t StackFreeBytes() {
    return stack_high > stack_low ? step_seq::StackProbe::FreeBytes(stack_low, stack_high) : 0;
}
#endif

static void ServiceStats() {
    device.counters[sq::CntMidiQueueOverflow] = input_dropped + all_off_requests.load(std::memory_order_relaxed);
    device.counters[sq::CntCallbackOverruns] = meter.Overruns();
    device.counters[sq::CntCallbackPeak] = meter.Peak();
    device.counters[sq::CntQueueHighWater] = commands.HighWater();
    static uint32_t last_scan = 0;
    if(System::GetNow() - last_scan >= 500) {
        last_scan = System::GetNow();
        device.counters[sq::CntStackFree] = StackFreeBytes();
    }
}

static void ServiceLeds(uint32_t now) {
    static uint32_t last_led = 0, shown = ~0u;
    if(now - last_led < kLedPeriodMs)
        return;
    last_led = now;
    const uint32_t leds = led_state.load(std::memory_order_relaxed);
    if(leds == shown)
        return;
    static constexpr float kLevels[4] = {0.f, 0.08f, 0.3f, 0.8f};
    for(int i = 0; i < 16; ++i)
        hw.SetLed(i, kLevels[(leds >> (2 * i)) & 3]);
    hw.UpdateLeds(); // I2C LED driver stays out of the audio interrupt
    shown = leds;
}

static void ServiceBootloader() {
    if(!boot_request.Pending())
        return;
    // Silence first: a panic ramp, then the reset.
    const uint32_t before = panic_ack.load(std::memory_order_acquire);
    RequestPanic();
    const uint32_t start = System::GetNow();
    while(panic_ack.load(std::memory_order_acquire) == before && System::GetNow() - start < 50)
        System::Delay(1);
    System::Delay(kPanicSettleMs);
    bootloader::Service(boot_request, hw, [](uint8_t* bytes, size_t size) { SendUsb(bytes, size); });
}

static uint32_t ReadUid(int word) {
    return word == 0 ? HAL_GetUIDw0() : (word == 1 ? HAL_GetUIDw1() : HAL_GetUIDw2());
}
} // namespace fw

static void Initialize() {
    hw.Init();
    hw.seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.seed.SetAudioBlockSize(fw::kBlockSize);
    for(auto& p : hw.pot)
        p.SetSampleRate(hw.seed.AudioCallbackRate());
    for(auto& c : hw.cv)
        c.SetSampleRate(hw.seed.AudioCallbackRate());
    Random::Init();
    for(int i = 0; i < 3; ++i)
        fw::device.uid[i] = fw::ReadUid(i);
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "streichwurst";
    fw::device.version = STREICHWURST_VERSION;
    fw::device.protocol_minor = 1; // SQ 1.1: CapModMatrix, KindProduct
    fw::device.capabilities = sq::CapVoice | sq::CapDin | sq::CapMonitor | sq::CapModMatrix;
    fw::device.components = fw::components;
    fw::device.component_count = fw::kComponents;
    if(!product.Prepare(48000.f, fw::kBlockSize, key_memory, fx_memory)) // clears the reverb lines
        while(true) {
        }
    product.Update(state, nullptr);
    fw::last.state = state;
    for(uint8_t k = 0; k < fw::kComponents; ++k)
        for(uint8_t i = 0; i < fw::Table(k).count; ++i)
            fw::ReadParam(state, k, fw::Table(k).params[i].id, fw::values[k][i]);
    modulation.Init(kModInfo, 48000.f, fw::kBlockSize);
    modulation.EnableExtensions({true, true, false}); // extended monitor, LFO cross modulation
    meter.Init((SystemCoreClock / 48000u) * uint32_t(fw::kBlockSize));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MidiUsbHandler::Config usb_config;
    usb_config.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    usb_midi.Init(usb_config);
    usb_midi.StartReceive();
    hw.midi.StartReceive();
    fw::StackPaint();
}

// One pass of the main loop.
static void ServiceMain() {
    const uint32_t now = System::GetNow();
    fw::ServiceUsbState();
    fw::ServiceInput();
    fw::ServiceSnapshot();
    fw::ServiceStats();
    modulation.controller.Service(now, mod_sender);
    fw::device.Service(now);
    fw::ServiceLeds(now);
    fw::ServiceBootloader();
}

int main() {
    asm volatile("" : : "r"(streichwurst_firmware_version)); // Keep the version in the BIN.
    Initialize();
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
