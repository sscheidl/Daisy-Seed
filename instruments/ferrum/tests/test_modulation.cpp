// SPDX-License-Identifier: MIT
// Separate Mod on Daisy build of the looper (LOOPER_MOD): the production main.cpp
// answers the editor's SysEx over J7, the matrix modulates the pot and CC values
// once per block, the looper's own CCs and the bootloader SysEx keep working, and
// clearing the matrix returns every control exactly to its base.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
#include "memory_volume.h"
#include "mod_on_daisy/protocol.h"
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

static MemoryVolume memoryCard;
namespace looper {
bool InitCard(bool (*)()) { return false; }  // no card: storage stays off
Volume& CardVolume() { return memoryCard; }
}  // namespace looper
#include "sd_management_fakes.h"

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

namespace {
using looper::columns::Column;
constexpr size_t kBlock = 48;
float il[kBlock], ir[kBlock], ol[kBlock], orr[kBlock];
float inputLevel = 0.f;
uint64_t t = 0;
uint8_t seq = 0;

// One audio block plus one pass of the main loop.
void Tick(unsigned count = 1) {
    const float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    for (unsigned n = 0; n < count; ++n) {
        ServiceUsbMidi();
        for (size_t i = 0; i < kBlock; ++i, ++t) {
            il[i] = inputLevel * float(std::sin(2.0 * 3.14159265358979 * 220.0 * double(t) / 48000.0));
            ir[i] = 0.f;
        }
        AudioCallback(in, out, kBlock);
        modulation.controller.Service(System::now, modSender);
        ServiceBootloader();
        ++System::now;
        for (size_t i = 0; i < kBlock; ++i)
            CHECK(std::isfinite(ol[i]) && std::isfinite(orr[i]) && std::fabs(ol[i]) <= 1.f);
    }
}
void Press(unsigned key) {
    hw.pressed[key] = true;
    Tick(15);
    hw.pressed[key] = false;
    Tick(15);
}
void Midi(daisy::MidiMessageType type, uint8_t a, uint8_t b = 0, int channel = 0) {
    daisy::MidiEvent e;
    e.type = type;
    e.channel = channel;
    e.data[0] = a;
    e.data[1] = b;
    usbMidi.incoming.push_back(e);
}
void Cc(uint8_t controller, uint8_t value) {
    Midi(daisy::ControlChange, controller, value);
    Tick(2);
}
void Tap(uint8_t controller) {
    Cc(controller, 127);
    Cc(controller, 0);
}

// Sends an editor request and returns the payload of the one reply.
std::vector<uint8_t> Request(uint8_t command, std::vector<uint8_t> payload, uint8_t* replyCommand = nullptr) {
    uint8_t frame[mod::proto::kMaxFrame];
    seq = uint8_t((seq + 1) & 127);
    const size_t size = mod::proto::BuildFrame(frame, 0, command, seq, payload.data(), payload.size());
    daisy::MidiEvent event;
    event.type = daisy::SystemCommon;
    event.sc_type = daisy::SystemExclusive;
    std::memcpy(event.sysex_data, frame + 1, size - 2);
    event.sysex_message_len = uint8_t(size - 2);
    usbMidi.incoming.push_back(event);
    usbMidi.sent.clear();
    Tick();
    CHECK(usbMidi.sent.size() == 1);
    if (usbMidi.sent.size() != 1)
        return {};
    const std::vector<uint8_t>& reply = usbMidi.sent[0];
    mod::proto::Frame f;
    CHECK(mod::proto::ParseHeader(reply.data() + 1, reply.size() - 2, f) == mod::proto::Parse::Ok);
    CHECK(mod::proto::Verify(reply.data() + 1, reply.size() - 2, f) == 0 && f.seq == seq);
    if (replyCommand)
        *replyCommand = f.command;
    else
        CHECK(f.command == (command | mod::proto::kReplyBit));
    return std::vector<uint8_t>(f.payload, f.payload + f.length);
}
void SetCell(mod::Source s, unsigned target, int depth) {
    Request(mod::proto::SetCell,
            {uint8_t(s), uint8_t(target), uint8_t(std::abs(depth)), uint8_t((depth < 0 ? 1 : 0) | 2)});
}
void Clear() {
    Request(mod::proto::ClearMatrix, {});
    Tick(2);
}

// The last monitor frame over `ticks` blocks: base values and the loop mode.
struct Monitored {
    float base[mod::kTargets];
    unsigned mode, origin;
};
Monitored Monitor(unsigned ticks) {
    Monitored m{};
    Request(mod::proto::Monitor, {20});
    usbMidi.sent.clear();
    Tick(ticks);
    const std::vector<uint8_t>* last = nullptr;
    for (const std::vector<uint8_t>& message : usbMidi.sent)
        if (message.size() > 7 && message[6] == mod::proto::MonitorFrame)
            last = &message;
    CHECK(last != nullptr);
    if (!last)
        return m;
    mod::proto::Frame f;
    CHECK(mod::proto::ParseHeader(last->data() + 1, last->size() - 2, f) == mod::proto::Parse::Ok);
    CHECK(f.length == 91);  // extended monitor with the instrument mode
    mod::proto::Reader rd(f.payload, f.length);
    for (int i = 0; i < 18; ++i)
        rd.U14();
    for (float& b : m.base)
        b = mod::proto::DecodeSigned(rd.U14());
    m.mode = f.payload[89];
    m.origin = f.payload[90];
    Request(mod::proto::Monitor, {0});
    return m;
}

void TestIdentify() {
    std::vector<uint8_t> r = Request(mod::proto::Identify, {});
    mod::proto::Reader id(r.data(), r.size());
    for (int i = 0; i < 5; ++i)
        id.U7();
    char key[mod::kMaxKeyLength + 1]{}, version[13]{};
    id.Str(key, mod::kMaxKeyLength);
    id.Str(version, 12);
    CHECK(std::strcmp(key, "looper") == 0 && std::strcmp(version, LOOPER_VERSION) == 0);
    // extended monitor and LFO cross modulation; capabilities 2 bit 0: instrument modes
    CHECK(r.size() >= 2 && r[r.size() - 2] == (0x0A | 0x30 | 0x40) && r.back() == 0x01);
    // The columns as the editors read them (GET_TARGET).
    const char* names[] = {"Speed", "Start", "Size", "Slice", "Skip", "Inertia", "Fripp Decay", "Mix"};
    const char* spans[] = {"+-12 st", "100%", "100%", "100%", "100%", "100%", "100%", "+-50%"};
    for (uint8_t c = 0; c < mod::kTargets; ++c) {
        const std::vector<uint8_t> g = Request(mod::proto::GetTarget, {c});
        mod::proto::Reader rd(g.data(), g.size());
        CHECK(rd.U7() == c && rd.U7() == 1);  // present, global, not latched
        rd.U14();
        char name[13]{}, span[9]{};
        rd.Str(name, 12);
        rd.Str(span, 8);
        CHECK(rd.Ok() && std::strcmp(name, names[c]) == 0 && std::strcmp(span, spans[c]) == 0);
    }
    r = Request(mod::proto::GetAllowed, {});
    mod::proto::Reader allowed(r.data(), r.size());
    CHECK(allowed.Mask64() == ~uint64_t{0});
    for (uint8_t m = 0; m < 4; ++m) {
        const std::vector<uint8_t> g = Request(mod::proto::GetMode, {m});
        mod::proto::Reader rd(g.data(), g.size());
        char name[mod::kMaxModeText + 1]{}, hint[mod::kMaxModeText + 1]{};
        CHECK(rd.U7() == m && rd.U7() == 4);
        rd.Str(name, mod::kMaxModeText);
        rd.Str(hint, mod::kMaxModeText);
        CHECK(rd.Ok() && std::strcmp(name, looper::columns::kModes[m].name) == 0 && hint[0] == 'S');
    }
    std::printf("PASS identify: key looper, 8 columns, all cells, four loop modes over SysEx\n");
}

void TestSpeed() {
    // Mod wheel +100 on Speed: +12 semitones on top of the pot's whole semitone.
    CHECK(engine.VarispeedTarget() == 0.f);
    SetCell(mod::ModWheel, looper::columns::Speed, 100);
    Cc(1, 127);
    Tick(200);  // the 10 ms MIDI smoothing snaps to the exact value after ~140 ticks
    CHECK(std::fabs(engine.VarispeedTarget() - 12.f) < 1e-3f);
    // CC20 takes the knob over and stays the base: +5 st + 12 st.
    Cc(20, 69);
    CHECK(std::fabs(engine.VarispeedTarget() - 17.f) < 1e-3f);
    // The pot takes the knob back after a 2 % move; the clamp holds at +36.
    hw.pot[knobs::Varispeed].value = .9f;  // +30 st
    Tick(2);
    CHECK(std::fabs(engine.VarispeedTarget() - 36.f) < 1e-3f);
    hw.pot[knobs::Varispeed].value = .4f;
    Cc(1, 0);
    Tick(200);
    CHECK(std::fabs(engine.VarispeedTarget()) < 1e-3f);
    // LFO 1 at about 5 Hz on Speed at depth 50: +-6 semitones, unrounded.
    SetCell(mod::Lfo1, looper::columns::Speed, 50);
    Request(mod::proto::SetSource, {mod::Lfo1, mod::ParamRate, 0x00, 0x60});
    float low = 100.f, high = -100.f;
    bool fractional = false;
    for (int i = 0; i < 1000; ++i) {
        Tick();
        const float s = engine.VarispeedTarget();
        low = std::fmin(low, s);
        high = std::fmax(high, s);
        fractional |= s != std::round(s);
    }
    CHECK(low < -5.9f && low >= -6.01f && high > 5.9f && high <= 6.01f && fractional);
    Clear();
    CHECK(engine.VarispeedTarget() == 0.f);  // exactly back on the pot's semitone
    std::printf("PASS speed: pot or CC20 base + matrix, clamped at +36, LFO +-%.2f st\n", double(high));
}

void TestWindowAndDecay() {
    // CV 1 on Start, the mod wheel on Size and Frippertronics decay.
    hw.pot[knobs::Start].value = .2f;
    hw.pot[knobs::Size].value = 1.f;
    hw.pot[knobs::Decay].value = .3f;
    Tick(20);
    const uint32_t start = engine.WindowStart();
    CHECK(start > 0 && engine.WindowSize() == engine.Length());  // size 1: the whole loop
    SetCell(mod::Cv1, looper::columns::Start, 50);
    hw.cv[0].value = 1.f;
    Tick(50);
    CHECK(engine.WindowStart() > start);
    SetCell(mod::ModWheel, looper::columns::Size, -100);
    SetCell(mod::ModWheel, looper::columns::Decay, 50);
    Cc(1, 127);
    Tick(200);
    CHECK(engine.WindowSize() < engine.Length() / 4);
    CHECK(std::fabs(engine.FrippertronicsDecay() - .8f) < 1e-3f);
    // CC27 moves the decay base; the offset stays on top, clamped at 1.
    Cc(27, 127);
    CHECK(engine.FrippertronicsDecay() == 1.f);
    hw.pot[knobs::Decay].value = .35f;  // the pot takes the knob back
    Tick(2);
    CHECK(std::fabs(engine.FrippertronicsDecay() - .85f) < 1e-3f);
    Cc(1, 0);
    hw.cv[0].value = 0.f;
    Clear();
    Tick(200);
    CHECK(engine.WindowStart() == start && engine.FrippertronicsDecay() == .35f);
    CHECK(engine.WindowSize() == engine.Length());
    hw.pot[knobs::Start].value = 0.f;
    Tick(20);
    std::printf("PASS window and decay: CV 1 on Start, wheel on Size and decay, CC27 base\n");
}

void TestSliceSkipInertia() {
    SetCell(mod::ModWheel, looper::columns::Slice, 100);
    SetCell(mod::ModWheel, looper::columns::Inertia, 70);
    Cc(1, 127);
    Tick(200);
    CHECK(engine.SliceDivisor() == 16 && std::fabs(engine.Inertia() - .7f) < 1e-3f);
    Cc(1, 0);
    Tick(200);
    CHECK(engine.SliceDivisor() == 1 && engine.Inertia() == 0.f);
    Clear();
    std::printf("PASS slice and inertia from the mod wheel\n");
}

// Mean absolute output minus the dry input over `blocks` blocks.
std::pair<float, float> Levels(unsigned blocks) {
    double loop = 0.0, total = 0.0;
    for (unsigned b = 0; b < blocks; ++b) {
        Tick();
        for (size_t i = 0; i < kBlock; ++i) {
            loop += std::fabs(ol[i] - il[i]);
            total += std::fabs(ol[i]);
        }
    }
    return {float(loop / (blocks * kBlock)), float(total / (blocks * kBlock))};
}

void TestMix() {
    inputLevel = .3f;
    const auto unity = Levels(100);  // dry input plus the loop
    CHECK(unity.first > .01f);
    // Wheel at -100: loop silent, the output is the dry input exactly.
    SetCell(mod::ModWheel, looper::columns::Mix, -100);
    Cc(1, 127);
    Tick(200);
    CHECK(mixGains.dry == 1.f && mixGains.loop == 0.f);
    CHECK(Levels(50).first == 0.f);
    // +100: dry silent, only the loop is heard.
    SetCell(mod::ModWheel, looper::columns::Mix, 100);
    Tick(200);
    CHECK(mixGains.dry == 0.f && mixGains.loop == 1.f);
    inputLevel = 0.f;
    const float loopOnly = Levels(50).second;
    inputLevel = .3f;
    CHECK(loopOnly > .01f);
    Cc(1, 0);
    Clear();
    CHECK(mixGains.dry == 1.f && mixGains.loop == 1.f);
    inputLevel = 0.f;
    std::printf("PASS mix: dry only, loop only, unity again\n");
}

void TestLooperMidiAndModes() {
    // The looper's CCs act with an active matrix; mode changes reach the monitor.
    SetCell(mod::Lfo2, looper::columns::Skip, 40);
    const bool reversed = engine.Reversed();
    Tap(105);
    Tick(20);
    CHECK(engine.Reversed() != reversed);
    Tap(105);
    Tap(112);  // CC112: Frippertronics
    CHECK(engine.GetLoopMode() == looper::LoopMode::Frippertronics);
    Monitored m = Monitor(60);
    CHECK(m.mode == 2 && m.origin == mod::kModeFromMidi);
    CHECK(std::fabs(m.base[looper::columns::Speed] - .4f) < 1e-3f);  // unity
    CHECK(std::fabs(m.base[looper::columns::Mix] - .5f) < 1e-3f);
    Press(keys::kFirstMode + 1);  // S10: Replace
    m = Monitor(60);
    CHECK(m.mode == 1 && m.origin == mod::kModeFromDevice);
    Press(keys::LoopMode);  // S6 cycles to Frippertronics
    CHECK(Monitor(60).mode == 2);
    Tap(110);
    CHECK(engine.GetLoopMode() == looper::LoopMode::SoundOnSound);
    Clear();
    std::printf("PASS looper CCs with an active matrix, loop mode and origin in the monitor\n");
}

void TestConsumedAndPassedOn() {
    // CC1, aftertouch, clock, start, notes and CC120/123 change no looper state.
    const auto mode = engine.GetLoopMode();
    const bool playing = engine.Playing();
    const float speed = engine.VarispeedTarget();
    usbMidi.sent.clear();
    Midi(daisy::ControlChange, 1, 100);
    Midi(daisy::ChannelPressure, 90);
    Midi(daisy::PolyphonicKeyPressure, 60, 80);
    Midi(daisy::NoteOn, 60, 100);
    Midi(daisy::ControlChange, 123, 0);
    Midi(daisy::ControlChange, 120, 0);
    daisy::MidiEvent clock;
    clock.type = daisy::SystemRealTime;
    clock.srt_type = daisy::TimingClock;
    usbMidi.incoming.push_back(clock);
    clock.srt_type = daisy::Start;
    usbMidi.incoming.push_back(clock);
    Tick(20);
    CHECK(engine.GetLoopMode() == mode && engine.Playing() == playing &&
          engine.VarispeedTarget() == speed && usbMidi.sent.empty());
    CHECK(modulation.live.ModWheelRaw() == 100 && modulation.live.ChannelPressureRaw() == 90);
    Cc(1, 0);
    std::printf("PASS CC1, aftertouch, clock and notes reach the matrix, not the looper\n");
}

void TestBootloader() {
    // The bootloader request is no Mod on Daisy frame: answered, then the reset.
    daisy::MidiEvent boot;
    boot.type = daisy::SystemCommon;
    boot.sc_type = daisy::SystemExclusive;
    std::copy(std::begin(bootloader::kRequestBody), std::end(bootloader::kRequestBody), boot.sysex_data);
    boot.sysex_message_len = sizeof(bootloader::kRequestBody);
    const uint32_t rejected = modulation.controller.RejectedFrames();
    usbMidi.sent.clear();
    usbMidi.incoming.push_back(boot);
    Tick();
    const std::vector<uint8_t> ack(std::begin(bootloader::kAck), std::end(bootloader::kAck));
    CHECK(usbMidi.sent.size() == 1 && usbMidi.sent[0] == ack);
    CHECK(System::bootloader_resets == 1 && modulation.controller.RejectedFrames() == rejected);
    std::printf("PASS bootloader SysEx: acknowledged and reset, not seen by the matrix\n");
}
}  // namespace

int main() {
    hw.Init();
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    auto* a = reinterpret_cast<looper::Frame*>(storageA);
    auto* b = reinterpret_cast<looper::Frame*>(storageB);
    std::uninitialized_fill_n(a, LOOPER_BUFFER_FRAMES, looper::Frame{});
    std::uninitialized_fill_n(b, LOOPER_BUFFER_FRAMES, looper::Frame{});
    CHECK(engine.Init(48000.f, a, b, LOOPER_BUFFER_FRAMES));
    InitModulation(48000.f);
    hw.pot[knobs::Varispeed].value = .4f;  // unity
    hw.pot[knobs::Slope].value = .5f;
    hw.pot[knobs::Size].value = 1.f;
    Tick(20);
    // A one-second loop to modulate.
    inputLevel = .5f;
    Press(keys::Record);
    Tick(1000);
    Press(keys::Record);
    inputLevel = 0.f;
    CHECK(engine.GetState() == looper::State::Looping && engine.Length() > 46000);
    TestIdentify();
    TestSpeed();
    TestWindowAndDecay();
    TestSliceSkipInertia();
    TestMix();
    TestLooperMidiAndModes();
    TestConsumedAndPassedOn();
    TestBootloader();
    if (failures) {
        std::printf("FAILED: %d modulation check(s)\n", failures);
        return 1;
    }
    std::printf("All MOD build tests passed\n");
    return 0;
}
