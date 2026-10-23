// Separate Mod on Daisy build of Physical Modeling (MOD=1): SysEx configuration,
// global offsets through the 5 ms parameter smoothing, Poly AT per Torus voice,
// latched excitation, and return to the upstream path.
#define main firmware_main
#include "../src/main.cpp"
#undef main
#include "mod_on_daisy/protocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace physical_modeling;
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }

namespace {
uint8_t seq = 0;
float left[48], right[48];
float* output[] = {left, right};

void Tick(unsigned count = 1) {
    for(unsigned n = 0; n < count; ++n) {
        ServiceMidi();
        AudioCallback(nullptr, output, 48);
        modulation.controller.Service(daisy::System::now, mod_sender);
        daisy::System::now++;
        for(unsigned i = 0; i < 48; ++i)
            assert(std::isfinite(left[i]) && std::isfinite(right[i]) && std::fabs(left[i]) < .8f && std::fabs(right[i]) < .8f);
    }
}

std::vector<uint8_t> Request(uint8_t command, std::vector<uint8_t> payload, uint8_t* reply_command = nullptr) {
    uint8_t frame[mod::proto::kMaxFrame];
    seq = uint8_t((seq + 1) & 127);
    const std::size_t size = mod::proto::BuildFrame(frame, 0, command, seq, payload.data(), payload.size());
    daisy::MidiEvent event;
    event.type = daisy::SystemCommon;
    event.sc_type = daisy::SystemExclusive;
    std::memcpy(event.sysex_data, frame + 1, size - 2);
    event.sysex_message_len = uint8_t(size - 2);
    midi.incoming.push_back(event);
    midi.sent.clear();
    Tick();
    assert(midi.sent.size() == 1);
    const std::vector<uint8_t>& reply = midi.sent[0];
    mod::proto::Frame f;
    assert(mod::proto::ParseHeader(reply.data() + 1, reply.size() - 2, f) == mod::proto::Parse::Ok);
    assert(mod::proto::Verify(reply.data() + 1, reply.size() - 2, f) == 0 && f.seq == seq);
    if(reply_command)
        *reply_command = f.command;
    else
        assert(f.command == (command | mod::proto::kReplyBit));
    return std::vector<uint8_t>(f.payload, f.payload + f.length);
}

void SetCell(mod::Source s, unsigned t, int depth) {
    Request(mod::proto::SetCell, {uint8_t(s), uint8_t(t), uint8_t(std::abs(depth)), uint8_t((depth < 0 ? 1 : 0) | 2)});
}
void Midi(daisy::MidiMessageType type, uint8_t a, uint8_t b = 0) {
    daisy::MidiEvent e;
    e.type = type;
    e.data[0] = a;
    e.data[1] = b;
    midi.incoming.push_back(e);
}
unsigned VoiceOf(int note) {
    for(unsigned v = 0; v < kVoices; ++v)
        if(std::lround(synth->VoiceNote(v)) == note)
            return v;
    assert(false && "note not on any voice");
    return 0;
}

void TestIdentify() {
    std::vector<uint8_t> r = Request(mod::proto::Identify, {});
    mod::proto::Reader id(r.data(), r.size());
    for(int i = 0; i < 5; ++i)
        id.U7();
    char key[mod::kMaxKeyLength + 1]{}, version[13]{};
    id.Str(key, mod::kMaxKeyLength);
    id.Str(version, 12);
    assert(std::strcmp(key, "physical-modeling") == 0 && std::strcmp(version, "0.1.0-mod") == 0);
    r = Request(mod::proto::GetAllowed, {});
    mod::proto::Reader allowed(r.data(), r.size());
    const uint64_t mask = allowed.Mask64();
    for(unsigned t = 0; t < 8; ++t)
        assert(bool(mask & mod::CellBit(mod::PolyAt, t)) == (t != Excitation)); // excitation latched
}

void TestGlobal() {
    // Mod wheel opens Dry/Wet fully; the 5 ms smoothing stays in charge.
    const float wet = synth->parameters.Get(Wet);
    SetCell(mod::ModWheel, Wet, 100);
    Midi(daisy::ControlChange, 1, 127);
    Tick();
    assert(synth->parameters.Get(Wet) > wet && synth->parameters.Get(Wet) < 1.0f); // smoothed, no jump
    Tick(200);
    assert(std::fabs(synth->parameters.Get(Wet) - 1.0f) < 1e-5f);
    // LFO 1 on pitch: +-12 semitones around the pot transposition.
    SetCell(mod::Lfo1, Frequency, 100);
    Request(mod::proto::SetSource, {mod::Lfo1, mod::ParamRate, 0x00, 0x60}); // ~5 Hz
    float low = 100, high = -100;
    for(int i = 0; i < 1000; ++i) {
        Tick();
        low = std::fmin(low, synth->parameters.PitchOffset());
        high = std::fmax(high, synth->parameters.PitchOffset());
    }
    std::printf("LFO pitch offset %.2f .. %.2f semitones\n", double(low), double(high));
    assert(low < -11.0f && high > 11.0f && low >= -12.01f && high <= 12.01f);
    Request(mod::proto::ClearMatrix, {});
    Tick(300);
    assert(synth->parameters.Get(Wet) == synth->parameters.Target(Wet)); // exactly back (snap)
    assert(std::fabs(synth->parameters.PitchOffset()) < 1e-5f);
}

void TestPolyAtPerVoice() {
    Midi(daisy::NoteOn, 60, 100);
    Midi(daisy::NoteOn, 64, 100);
    Tick(5);
    const unsigned v60 = VoiceOf(60), v64 = VoiceOf(64);
    assert(v60 != v64);
    SetCell(mod::PolyAt, Brightness, 100);
    SetCell(mod::PolyAt, Frequency, 50);
    Midi(daisy::PolyphonicKeyPressure, 60, 127);
    Tick(200);
    const float base = synth->parameters.Get(Brightness);
    assert(std::fabs(synth->VoicePatch(v60).brightness - std::fmin(1.0f, base + 1.0f)) < 1e-4f);
    assert(std::fabs(synth->VoicePatch(v64).brightness - base) < 1e-6f); // unpressed voice
    assert(std::fabs(synth->VoiceFm(v60) - 6.0f) < 1e-3f && synth->VoiceFm(v64) == 0.0f); // 50 % of +-12 st
    assert(synth->parameters.Get(Brightness) == base); // the global value is not touched
    // Note Off: pressure falls to 0, the ringing voice glides back.
    Midi(daisy::NoteOff, 60, 0);
    Tick(200);
    assert(std::fabs(synth->VoiceFm(v60)) < 1e-3f);
    Request(mod::proto::ClearMatrix, {});
    Tick(2);
}

void TestLatchedExcitation() {
    SetCell(mod::ChannelAt, Excitation, -100);
    Midi(daisy::ChannelPressure, 127);
    Tick(200);
    assert(synth->parameters.Get(Excitation) < 1e-4f); // next strikes get no energy
    Request(mod::proto::ClearMatrix, {});
    Tick(300);
    assert(synth->parameters.Get(Excitation) == synth->parameters.Target(Excitation));
}

void TestStress() {
    // Eight connections incl. S&H and random on voice targets while notes keep coming.
    SetCell(mod::Lfo1, Structure, 60);
    SetCell(mod::Lfo2, Position, -80);
    SetCell(mod::Random, Damping, 70);
    SetCell(mod::Cv1, Brightness, 50);
    SetCell(mod::ModWheel, Decay, 90);
    SetCell(mod::ChannelAt, Wet, 40);
    SetCell(mod::PolyAt, Damping, 100);
    SetCell(mod::Cv2, Frequency, 30);
    Request(mod::proto::SetSource, {mod::Lfo1, mod::ParamShape, mod::SampleHold, 0});
    Request(mod::proto::SetSource, {mod::Lfo1, mod::ParamRate, 0x7F, 0x7F});
    hw.cv[0].value = 0.7f;
    hw.cv[1].value = -0.4f;
    for(unsigned i = 0; i < 2000; ++i) {
        if(i % 40 == 0) {
            Midi(daisy::NoteOn, uint8_t(48 + i % 36), 110);
            Midi(daisy::PolyphonicKeyPressure, uint8_t(48 + i % 36), uint8_t(i % 128));
            Midi(daisy::ControlChange, 1, uint8_t(i % 128));
            Midi(daisy::ChannelPressure, uint8_t((i * 3) % 128));
        }
        Tick();
    }
    Request(mod::proto::ClearMatrix, {});
    Tick(10);
}
}

int main() {
    hw.Init();
    cpu_meter.Init(48000, 48);
    InitializeSynth();
    InitModulation(48000.0f);
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    for(auto& pot : hw.pot)
        pot.value = .5f;
    Tick(50);
    TestIdentify();
    TestGlobal();
    TestPolyAtPerVoice();
    TestLatchedExcitation();
    TestStress();
    std::printf("PASS: Physical Modeling Mod on Daisy build: SysEx, global offsets, Poly AT per voice, latched excitation, stress\n");
}
