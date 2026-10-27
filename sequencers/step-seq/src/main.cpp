// SPDX-License-Identifier: MIT
// Step Seq firmware for the Seed3 Desktop Dev Kit (ARBEITSAUFTRAG Phase 1): 2 x 8 steps
// on CV/Gate and MIDI (USB J7 and DIN), no audio. The audio callback (1 kHz) owns the
// controls, the sequencer core and the DAC; main owns USB/DIN MIDI, the SQ protocol, the
// LEDs and the bootloader entry. Hand-over only through SPSC queues and snapshots.
#include "version.h"
const char step_seq_firmware_version[] = "STEP-SEQ-FW-VERSION=" STEP_SEQ_FIRMWARE_VERSION;
#include "desktop_devkit.h"
#include "bootloader_daisy.h"
#include "bootloader_midi.h"
#include "usbd_core.h"
#include "step_seq/controls.h"
#include "step_seq/cv.h"
#include "step_seq/device.h"
#include "step_seq/diagnostics.h"
#include "step_seq/engine.h"
#include "step_seq/midi_out.h"
#include "step_seq/params.h"
#include "step_seq/spsc.h"
#include <atomic>
using namespace daisy;
extern "C" USBD_HandleTypeDef hUsbDeviceHS;

namespace fw {
using namespace step_seq;
constexpr std::size_t kBlockSize = kBlock; // 1 kHz control rate
// SW17 on Run when the toggle reads pressed (hardware test 3 confirms the direction).
constexpr bool kRunWhenPressed = true;
constexpr uint16_t kStartupDacCode = 0; // DAC code until the CV outputs are enabled (H14)
constexpr uint32_t kLedPeriodMs = 10;
constexpr int kDinByteBudget = 24;
constexpr uint32_t kPanicSettleMs = 5;

struct Command {
    enum class Kind : uint8_t { Param, Pattern, Step, Transport, Cal, CvEnable, Panic };
    Kind kind = Kind::Param;
    uint16_t id = 0, revision = 0;
    int32_t value = 0;
    uint8_t index = 0;
    Pattern pattern{};
    OutputCal cal;
};
struct MidiIn {
    enum class Kind : uint8_t { Clock, Start, Stop, Continue };
    Kind kind = Kind::Clock;
    Port port = Port::Usb;
};
struct Snapshot {
    sq::View view;
    Settings settings;
    uint32_t edits = 0;
    uint32_t clock_ignored = 0;
};
// LED mailbox: 2 bits per LED (off, dim, mid, bright), rendered by main at most every 10 ms.
enum Led : uint32_t { kLedOff = 0, kLedDim = 1, kLedMid = 2, kLedBright = 3 };
} // namespace fw

static desktop_devkit::Hardware hw;
static step_seq::Engine engine;
static step_seq::Controls controls;
static step_seq::NoteFeed feed;
static step_seq::CvOut cv_out;
static step_seq::CvQuantizer cv1_quantizer;
static step_seq::CallbackMeter meter;
static step_seq::SpscQueue<fw::Command, 16> commands;
static step_seq::SpscQueue<fw::MidiIn, 64> midi_in;
static step_seq::TripleBuffer<fw::Snapshot> snapshots;
static std::atomic<uint32_t> led_state{0};
static std::atomic<uint32_t> panic_ack{0};
static bootloader::Request boot_request;
static MidiUsbHandler usb_midi;
static step_seq::DacWriter<DAC_TypeDef> dac(DAC1);
static bool gate1_last = false, gate2_last = false;
static uint32_t control_ms = 0;
static uint32_t panic_count = 0;

// ----- audio callback: controls, sequencer, CV ------------------------------------------
static void ApplyCommand(const fw::Command& c) {
    using K = fw::Command::Kind;
    switch(c.kind) {
        case K::Param:
            step_seq::WriteParam(engine.settings, c.id, c.value);
            if(step_seq::PotOfParam(c.id) >= 0)
                controls.ReleaseGlobal(step_seq::PotOfParam(c.id));
            engine.ParameterApplied(c.revision);
            break;
        case K::Pattern: engine.QueuePattern(c.pattern, c.revision); break;
        case K::Step: engine.QueueStep(c.index, c.pattern[c.index], c.revision); break;
        case K::Transport:
            if(c.value)
                engine.PanelStart();
            else
                engine.PanelStop();
            break;
        case K::Cal: {
            cv_out.SetCal(c.index, c.cal);
            if(c.index == 0)
                step_seq::NoteRange(cv_out.cal[0], engine.settings.cv_low_note, engine.settings.cv_high_note);
            break;
        }
        case K::CvEnable: cv_out.Enable(c.value != 0); break;
        case K::Panic:
            engine.Panic();
            ++panic_count;
            break;
    }
}

static uint32_t LedFrame(const step_seq::TickOutput& out) {
    uint32_t leds = 0;
    const int playing = out.state == step_seq::State::Running ? out.step : -1;
    const bool blink = (control_ms / 62) & 1; // ~8 Hz for the selected step
    for(int i = 0; i < step_seq::kSteps; ++i) {
        const auto& s = engine.pattern[std::size_t(i)];
        uint32_t level = s.gate ? fw::kLedBright : fw::kLedOff;
        if(i == playing)
            level = s.gate ? fw::kLedDim : fw::kLedMid; // playhead: dims a lit step, flashes a dark one
        if(i == controls.Selected() && blink)
            level = level == fw::kLedOff ? fw::kLedMid : fw::kLedOff;
        leds |= level << (2 * i);
    }
    return leds;
}

static void AudioCallback(AudioHandle::InputBuffer, AudioHandle::OutputBuffer out, size_t n) {
    meter.Begin(DWT->CYCCNT);
    for(size_t i = 0; i < n; ++i)
        out[0][i] = out[1][i] = 0.f; // no audio in this firmware
    if(n != fw::kBlockSize)
        return;
    ++control_ms;
    hw.UpdateAllControls();
    fw::Command c;
    while(commands.Pop(c))
        ApplyCommand(c);
    if(panic_count)
        panic_ack.store(panic_count, std::memory_order_release);
    fw::MidiIn m;
    while(midi_in.Pop(m)) {
        switch(m.kind) {
            case fw::MidiIn::Kind::Clock: engine.MidiClock(m.port); break;
            case fw::MidiIn::Kind::Start: engine.MidiStart(m.port); break;
            case fw::MidiIn::Kind::Stop: engine.MidiStop(m.port); break;
            case fw::MidiIn::Kind::Continue: engine.MidiContinue(m.port); break;
        }
    }
    step_seq::ControlFrame frame;
    for(int i = 0; i < 8; ++i)
        frame.pots[i] = hw.pot[i].Value();
    for(int i = 0; i < 16; ++i)
        if(hw.button[i].Pressed())
            frame.keys |= uint16_t(1u << i);
    const int sw = hw.tog_sw18.Read();
    frame.sw18 = sw == Switch3::POS_UP ? 0 : (sw == Switch3::POS_DOWN ? 2 : 1);
    frame.ms = control_ms;
    if(controls.Process(frame, engine)) {
        engine.Panic(); // gates and notes are released before the bootloader (4.1)
        boot_request.Set(bootloader::Request::kKeys);
    }

    step_seq::TickInput in;
    in.run_switch = hw.tog_sw17.Pressed() == fw::kRunWhenPressed;
    const bool g1 = hw.gate_in[0].State(), g2 = hw.gate_in[1].State();
    in.gate1_rise = g1 && !gate1_last;
    in.gate2_rise = g2 && !gate2_last;
    in.gate2_high = g2;
    gate1_last = g1;
    gate2_last = g2;
    in.cv1_semitones = cv1_quantizer.Process(hw.cv[0].Value());
    in.cv2 = hw.cv[1].Value();
    static step_seq::TickOutput result;
    engine.Tick(in, result);
    feed.Feed(result);
    uint16_t pitch_code = 0, gate_code = 0;
    if(cv_out.Tick(result.gate, result.pitch, result.glide, engine.settings.glide_ms, pitch_code, gate_code))
        dac.Write(pitch_code, gate_code); // the only DAC writer after start-up (R01)

    fw::Snapshot& s = snapshots.Back();
    s.view.state = uint8_t(result.state);
    s.view.mode = uint8_t(engine.settings.mode);
    s.view.sw17 = in.run_switch;
    s.view.sw18 = uint8_t(controls.Sw18());
    s.view.active = uint8_t(result.active == step_seq::Active::None ? step_seq::Active::Internal : result.active);
    s.view.step = result.step;
    s.view.gate = result.gate;
    s.view.tempo_milli = int32_t(engine.CurrentBpm() * 1000.f);
    s.view.applied_revision = engine.AppliedRevision();
    s.view.cv_enabled = cv_out.Enabled();
    s.view.pattern = engine.pattern;
    s.settings = engine.settings;
    s.edits = controls.edits_;
    s.clock_ignored = engine.counters.clock_ignored;
    snapshots.Publish();
    led_state.store(LedFrame(result), std::memory_order_relaxed);
    meter.End(DWT->CYCCNT);
}

// ----- main loop: MIDI, SQ protocol, LEDs, bootloader --------------------------------------
namespace fw {
static bool usb_connected = false;
static void SendUsb(const uint8_t* bytes, std::size_t size) {
    if(usb_connected)
        usb_midi.SendMessage(const_cast<uint8_t*>(bytes), size);
}
static void SendDin(const uint8_t* bytes, std::size_t size) { hw.midi.SendMessage(const_cast<uint8_t*>(bytes), size); }
struct UsbSend {
    void operator()(uint8_t* b, int n) const { SendUsb(b, std::size_t(n)); }
};
struct DinSend {
    void operator()(uint8_t* b, int n) const { SendDin(b, std::size_t(n)); }
};
static MidiPort<UsbSend> usb_port(UsbSend{}, 1 << 30);
static MidiPort<DinSend> din_port(DinSend{}, kDinByteBudget);
static int32_t values[sq::kStepSeqComponent.count];
static sq::Component components[] = {
    {"step-seq", sq::KindSequencer, STEP_SEQ_VERSION, &sq::kStepSeqComponent, values},
};

class Host : public sq::Host {
  public:
    bool PushParam(uint8_t, uint16_t id, int32_t value, uint16_t revision) override {
        Command c;
        c.kind = Command::Kind::Param;
        c.id = id;
        c.value = value;
        c.revision = revision;
        return commands.Push(c);
    }
    bool PushPattern(const Pattern& p, uint16_t revision) override {
        Command c;
        c.kind = Command::Kind::Pattern;
        c.pattern = p;
        c.revision = revision;
        return commands.Push(c);
    }
    bool PushStep(uint8_t index, const Step& s, uint16_t revision) override {
        Command c;
        c.kind = Command::Kind::Step;
        c.index = index;
        c.pattern[index] = s;
        c.revision = revision;
        return commands.Push(c);
    }
    bool PushTransport(uint8_t command) override {
        // Allowed only with enable: Start from PAUSE, Stop while running (4.3).
        const sq::View& v = snapshots.Front().view;
        const bool ok = v.sw17 && (command ? v.state == uint8_t(State::Paused) : v.state == uint8_t(State::Running));
        if(!ok)
            return false;
        Command c;
        c.kind = Command::Kind::Transport;
        c.value = command;
        return commands.Push(c);
    }
    bool PushCal(uint8_t output, const OutputCal& cal) override {
        Command c;
        c.kind = Command::Kind::Cal;
        c.index = output;
        c.cal = cal;
        return commands.Push(c);
    }
    bool PushCvEnable(bool on) override {
        Command c;
        c.kind = Command::Kind::CvEnable;
        c.value = on;
        return commands.Push(c);
    }
    void Send(const uint8_t* bytes, std::size_t size) override { SendUsb(bytes, size); }
};
static Host host;
static sq::Device device(host);
static Settings last_settings;
static uint8_t last_channel = 1;
static bool last_usb_out = true, last_din_out = true;

static void RequestPanic() {
    Command c;
    c.kind = Command::Kind::Panic;
    commands.Push(c);
}

static void HandleRealtime(const MidiEvent& e, Port port) {
    MidiIn m;
    m.port = port;
    switch(e.srt_type) {
        case TimingClock: m.kind = MidiIn::Kind::Clock; break;
        case Start: m.kind = MidiIn::Kind::Start; break;
        case Stop: m.kind = MidiIn::Kind::Stop; break;
        case Continue: m.kind = MidiIn::Kind::Continue; break;
        default: return;
    }
    midi_in.Push(m);
}

static void ServiceInput() {
    usb_midi.Listen(); // restarts reception after an overflow
    while(usb_midi.HasEvents()) {
        const MidiEvent e = usb_midi.PopEvent();
        if(e.type == SystemRealTime) {
            HandleRealtime(e, Port::Usb);
        } else if(e.type == SystemCommon && e.sc_type == SystemExclusive) {
            if(bootloader::IsRequest(e)) {
                boot_request.Set(bootloader::Request::kSysEx);
                continue;
            }
            uint8_t frame[sq::kMaxFrame + 16];
            const std::size_t n = sq::FromSysExBody(e.sysex_data, e.sysex_message_len, frame, sizeof(frame));
            if(n)
                device.Handle(frame, n, System::GetNow());
        }
    }
    hw.midi.Listen();
    while(hw.midi.HasEvents()) {
        const MidiEvent e = hw.midi.PopEvent();
        if(e.type == SystemRealTime)
            HandleRealtime(e, Port::Din); // SysEx over DIN is not part of this version (4.4)
    }
}

static void ServiceSnapshot() {
    if(!snapshots.Update())
        return;
    const Snapshot& s = snapshots.Front();
    device.view = s.view;
    device.view.usb_connected = usb_connected;
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i) {
        const uint16_t id = sq::kStepSeqComponent.params[i].id;
        int32_t value = 0;
        int32_t old = 0;
        ReadParam(s.settings, id, value);
        ReadParam(last_settings, id, old);
        values[i] = value;
        if(value != old)
            device.ParamChanged(0, id);
    }
    last_settings = s.settings;
    // Channel change and switched-off outputs: Note-Offs on the old setting first (4.4).
    if(s.settings.seq_channel != last_channel) {
        usb_port.Panic(last_channel);
        din_port.Panic(last_channel);
        RequestPanic();
        last_channel = s.settings.seq_channel;
    }
    if(!s.settings.usb_out && last_usb_out) {
        usb_port.Panic(last_channel);
        RequestPanic();
    }
    if(!s.settings.din_out && last_din_out) {
        din_port.Panic(last_channel);
        RequestPanic();
    }
    usb_port.enabled = s.settings.usb_out && usb_connected;
    din_port.enabled = s.settings.din_out;
    last_usb_out = s.settings.usb_out;
    last_din_out = s.settings.din_out;
}

static void ServiceUsbState() {
    const bool connected = hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED;
    if(connected == usb_connected)
        return;
    usb_connected = connected;
    if(!connected) {
        usb_port.Forget(); // nothing arrives any more; clean up gates, DIN and the voice
        din_port.Panic(last_channel);
        RequestPanic();
    } else {
        usb_port.enabled = last_usb_out;
        usb_port.AllNotesOff(last_channel); // reconnect: All Notes Off first
    }
}

#ifdef STEP_SEQ_HOST_TEST
static void StackPaint() {}
static uint32_t StackFreeBytes() { return 0; }
#else
// Stack high-water: paint DTCM between the end of .dtcmram_bss and the current stack.
extern "C" uint32_t _edtcmram_bss;
static uint32_t *stack_low = nullptr, *stack_high = nullptr;
static void StackPaint() {
    uint32_t sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    stack_low = &_edtcmram_bss;
    stack_high = reinterpret_cast<uint32_t*>((sp - 512u) & ~3u);
    if(stack_high > stack_low)
        StackProbe::Paint(stack_low, stack_high);
}
static uint32_t StackFreeBytes() { return stack_high > stack_low ? StackProbe::FreeBytes(stack_low, stack_high) : 0; }
#endif

static void ServiceStats() {
    device.counters[sq::CntNoteOnDropped] = feed.dropped_on.load(std::memory_order_relaxed);
    device.counters[sq::CntMidiQueueOverflow] = feed.overflow.load(std::memory_order_relaxed);
    device.counters[sq::CntClockIgnored] = snapshots.Front().clock_ignored;
    device.counters[sq::CntCallbackOverruns] = meter.Overruns();
    device.counters[sq::CntCallbackPeak] = meter.Peak();
    uint32_t high = feed.queue.HighWater();
    if(commands.HighWater() > high)
        high = commands.HighWater();
    if(midi_in.HighWater() > high)
        high = midi_in.HighWater();
    device.counters[sq::CntQueueHighWater] = high;
    static uint32_t last_scan = 0;
    if(System::GetNow() - last_scan >= 500) { // the scan reads up to ~100 KiB
        last_scan = System::GetNow();
        device.counters[sq::CntStackFree] = StackFreeBytes();
    }
}

static void ServiceLeds(uint32_t now) {
    static uint32_t last = 0, shown = ~0u;
    if(now - last < kLedPeriodMs)
        return;
    last = now;
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
    // Release gates and notes on every active output before the reset (4.1).
    const uint32_t before = panic_ack.load(std::memory_order_acquire);
    RequestPanic();
    const uint32_t start = System::GetNow();
    while(panic_ack.load(std::memory_order_acquire) == before && System::GetNow() - start < 50)
        System::Delay(1);
    System::Delay(kPanicSettleMs);
    usb_port.Panic(last_channel);
    din_port.Panic(last_channel);
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
    // DAC: hw.Init() configured polling with software trigger; one SetDacOut per channel
    // from main enables both. Afterwards only the callback writes registers (R01).
    hw.SetDacOut(DacHandle::Channel::BOTH, fw::kStartupDacCode);
    Random::Init();
    for(int i = 0; i < 3; ++i)
        fw::device.uid[i] = fw::ReadUid(i);
    fw::device.boot_id = Random::GetValue() ^ fw::device.uid[0];
    fw::device.product = "step-seq";
    fw::device.version = STEP_SEQ_FIRMWARE_VERSION;
    fw::device.capabilities = sq::CapCv | sq::CapDin | sq::CapPattern | sq::CapMonitor | sq::CapCalibration;
    fw::device.components = fw::components;
    fw::device.component_count = 1;
    engine.Init(); // fixed non-zero seed; runs on across start and stop (4.2)
    fw::last_settings = engine.settings;
    for(uint8_t i = 0; i < sq::kStepSeqComponent.count; ++i)
        step_seq::ReadParam(engine.settings, sq::kStepSeqComponent.params[i].id, fw::values[i]);
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
    step_seq::ServiceMidi(feed, fw::last_channel, fw::last_settings.clock_out, fw::usb_port, fw::din_port);
    fw::device.Service(now);
    fw::ServiceLeds(now);
    fw::ServiceBootloader();
}

int main() {
    asm volatile("" : : "r"(step_seq_firmware_version)); // Keep the version in the BIN.
    Initialize();
    hw.seed.StartAudio(AudioCallback);
    while(true)
        ServiceMain();
}
