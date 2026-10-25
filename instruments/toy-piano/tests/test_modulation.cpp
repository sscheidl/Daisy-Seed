// Toy Piano (from v0.3) with Mod on Daisy, through the production callback and USB-MIDI
// path: SysEx configuration, per-voice pitch/level/harmonic, Poly AT per voice,
// latched attack/decay, the return to the unmodulated sound and (v0.4) the echo guard.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include "mod_on_daisy/protocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using toy_piano::kNumKeys;
uint8_t seq = 0;

void Tick(unsigned count = 1) {
    float left[toy_piano::kBlockSize], right[toy_piano::kBlockSize];
    float* outputs[] = {left, right};
    for(unsigned i = 0; i < count; ++i) {
        AudioCallback(nullptr, outputs, toy_piano::kBlockSize);
        ++daisy::System::now;
        for(std::size_t s = 0; s < toy_piano::kBlockSize; ++s)
            assert(std::isfinite(left[s]) && std::fabs(left[s]) < toy_piano::kOutputCeiling);
    }
}

daisy::MidiEvent Event(daisy::MidiMessageType type, uint8_t a, uint8_t b = 0) {
    daisy::MidiEvent event;
    event.type = type;
    event.data[0] = a;
    event.data[1] = b;
    return event;
}

// Queues a complete SysEx frame (F0 ... F7) as received USB-MIDI.
void Receive(const uint8_t* frame, std::size_t size) {
    daisy::MidiEvent event;
    event.type = daisy::SystemCommon;
    event.sc_type = daisy::SystemExclusive;
    std::memcpy(event.sysex_data, frame + 1, size - 2);
    event.sysex_message_len = uint8_t(size - 2);
    usb_midi.incoming.push_back(event);
}

// One SysEx request through the real USB path; returns the reply payload.
std::vector<uint8_t> Request(uint8_t command, std::vector<uint8_t> payload, uint8_t* reply_command = nullptr) {
    uint8_t frame[mod::proto::kMaxFrame];
    seq = uint8_t((seq + 1) & 127);
    const std::size_t size = mod::proto::BuildFrame(frame, 0, command, seq, payload.data(), payload.size());
    Receive(frame, size);
    usb_midi.sent.clear();
    ServiceUsbMidi();
    ++daisy::System::now;
    assert(usb_midi.sent.size() == 1);
    const std::vector<uint8_t>& reply = usb_midi.sent[0];
    mod::proto::Frame f;
    assert(mod::proto::ParseHeader(reply.data() + 1, reply.size() - 2, f) == mod::proto::Parse::Ok);
    assert(mod::proto::Verify(reply.data() + 1, reply.size() - 2, f) == 0 && f.seq == seq);
    if(reply_command)
        *reply_command = f.command;
    else
        assert(f.command == (command | mod::proto::kReplyBit));
    return std::vector<uint8_t>(f.payload, f.payload + f.length);
}

void SetCell(mod::Source source, toy_piano::ModTarget target, int depth) {
    Request(mod::proto::SetCell, {uint8_t(source), uint8_t(target), uint8_t(std::abs(depth)),
                                  uint8_t((depth < 0 ? 1 : 0) | 2)});
}

float Ratio(std::size_t voice, float base) { return piano.VoiceIncrement(voice) / base; }

void TestIdentifyAndColumns() {
    std::vector<uint8_t> r = Request(mod::proto::Identify, {});
    mod::proto::Reader id(r.data(), r.size());
    for(int i = 0; i < 5; ++i)
        id.U7();
    char key[mod::kMaxKeyLength + 1]{}, version[13]{};
    id.Str(key, mod::kMaxKeyLength);
    id.Str(version, 12);
    assert(std::strcmp(key, "toy-piano") == 0 && std::strcmp(version, TOY_PIANO_VERSION) == 0);
    for(uint8_t t = 5; t < 8; ++t)
        assert(Request(mod::proto::GetTarget, {t})[1] == 0); // free columns: greyed out
    r = Request(mod::proto::GetAllowed, {});
    mod::proto::Reader allowed(r.data(), r.size());
    const uint64_t mask = allowed.Mask64();
    assert(!(mask & mod::CellBit(mod::PolyAt, toy_piano::ModAttack)));
    assert(!(mask & mod::CellBit(mod::PolyAt, toy_piano::ModDecay)));
    assert(mask & mod::CellBit(mod::PolyAt, toy_piano::ModLevel));
    uint8_t command = 0;
    r = Request(mod::proto::SetCell, {mod::PolyAt, toy_piano::ModAttack, 50, 2}, &command);
    assert(command == mod::proto::kNak && r[1] == mod::proto::ErrNotAllowed);
}

void TestLfoVibrato() {
    usb_midi.incoming.push_back(Event(daisy::NoteOn, 69, 100));
    ServiceUsbMidi();
    Tick();
    const float base = piano.VoiceIncrement(kNumKeys); // A4, unmodulated
    const float key_base = piano.VoiceIncrement(0);
    SetCell(mod::Lfo1, toy_piano::ModPitch, 100); // +-2 semitones
    Request(mod::proto::SetSource, {mod::Lfo1, mod::ParamRate, 0x7F, 0x7F}); // 20 Hz
    float low = 10, high = 0;
    for(int i = 0; i < 200; ++i) {
        Tick();
        low = std::fmin(low, Ratio(kNumKeys, base));
        high = std::fmax(high, Ratio(kNumKeys, base));
    }
    std::printf("vibrato ratio %.5f .. %.5f\n", double(low), double(high));
    assert(std::fabs(low - std::exp2(-2.0f / 12)) < 0.003f && std::fabs(high - std::exp2(2.0f / 12)) < 0.003f);
    // Key voices follow the LFO too.
    assert(std::fabs(Ratio(0, key_base) - Ratio(kNumKeys, base)) < 1e-5f);
    Request(mod::proto::ClearMatrix, {});
    Tick(2);
    assert(piano.VoiceIncrement(kNumKeys) == base && piano.VoiceIncrement(0) == key_base); // exact
    assert(piano.VoiceLevel(kNumKeys) == 1.0f);
}

void TestModWheelLevelAndPolyAt() {
    // Two MIDI notes; Poly AT raises only the pressed one, the mod wheel lowers all.
    usb_midi.incoming = {Event(daisy::NoteOn, 60, 100), Event(daisy::NoteOn, 64, 100)};
    ServiceUsbMidi();
    Tick();
    std::size_t v60 = 99, v64 = 99;
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i) {
        if(piano.MidiVoiceNote(i) == 60) v60 = kNumKeys + i;
        if(piano.MidiVoiceNote(i) == 64) v64 = kNumKeys + i;
    }
    assert(v60 != 99 && v64 != 99);
    SetCell(mod::PolyAt, toy_piano::ModLevel, 100);
    usb_midi.incoming.push_back(Event(daisy::PolyphonicKeyPressure, 60, 127));
    ServiceUsbMidi();
    Tick(100);
    assert(std::fabs(piano.VoiceLevel(v60) - 2.0f) < 1e-4f); // gain 2 (+6 dB)
    assert(piano.VoiceLevel(v64) == 1.0f);                     // untouched voice
    assert(piano.VoiceLevel(0) == 1.0f);                       // keys have no Poly AT
    SetCell(mod::ModWheel, toy_piano::ModLevel, -100);
    usb_midi.incoming.push_back(Event(daisy::ControlChange, 1, 127));
    ServiceUsbMidi();
    Tick(100);
    assert(std::fabs(piano.VoiceLevel(v64)) < 1e-4f);          // mod wheel: silent
    assert(std::fabs(piano.VoiceLevel(v60) - 1.0f) < 1e-4f);   // -1 + pressure +1
    // Level changes ramp over the block: no jump between two samples.
    usb_midi.incoming.push_back(Event(daisy::ControlChange, 1, 0));
    ServiceUsbMidi();
    Tick();
    const float after_one = piano.VoiceLevel(v64);
    assert(after_one > 0.0f && after_one < 1.0f);
    Request(mod::proto::ClearMatrix, {});
    Tick(2);
    for(std::size_t v = 0; v < toy_piano::kNumVoices; ++v)
        assert(piano.VoiceLevel(v) == 1.0f);
    // A voice that fell silent while modulated starts its next note at level 1.
    SetCell(mod::ModWheel, toy_piano::ModLevel, -100);
    usb_midi.incoming.push_back(Event(daisy::ControlChange, 1, 127));
    ServiceUsbMidi();
    Tick(100); // mod wheel smoothing (10 ms) has settled
    for(std::size_t v = 0; v < toy_piano::kNumVoices; ++v)
        assert(piano.VoiceLevel(v) < 0.01f); // sounding or silent, all follow at once
    Request(mod::proto::ClearMatrix, {});
    Tick(1);
    for(std::size_t v = 0; v < toy_piano::kNumKeys; ++v)
        assert(piano.Envelope(v).Active() || piano.VoiceLevel(v) == 1.0f);
}

void TestLatchedAttack() {
    // Channel AT lengthens the attack of the next strike, not of a sounding note.
    hw.pot[0].value = 0.0f; // shortest attack
    Tick(50);
    usb_midi.incoming.push_back(Event(daisy::NoteOn, 72, 127));
    ServiceUsbMidi();
    Tick(5);
    std::size_t fast = 99;
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        if(piano.MidiVoiceNote(i) == 72) fast = i;
    const float fast_level = piano.MidiEnvelope(fast).Level();
    SetCell(mod::ChannelAt, toy_piano::ModAttack, 100);
    usb_midi.incoming.push_back(Event(daisy::ChannelPressure, 127));
    ServiceUsbMidi();
    Tick(50);
    usb_midi.incoming.push_back(Event(daisy::NoteOn, 74, 127));
    ServiceUsbMidi();
    Tick(5);
    std::size_t slow = 99;
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        if(piano.MidiVoiceNote(i) == 74) slow = i;
    assert(fast != 99 && slow != 99);
    std::printf("level 5 ms after strike: attack 2 ms %.3f, modulated %.3f\n", double(fast_level),
                double(piano.MidiEnvelope(slow).Level()));
    assert(fast_level > 0.9f && piano.MidiEnvelope(slow).Level() < 0.1f);
    Request(mod::proto::ClearMatrix, {});
}

void TestHarmonicAndAliasRule() {
    // Harmonic +100 adds 0.5 weight; above 12 kHz the harmonic stays off.
    SetCell(mod::ModWheel, toy_piano::ModHarmonic, 100);
    usb_midi.incoming = {Event(daisy::ControlChange, 1, 127), Event(daisy::NoteOn, 127, 100)};
    ServiceUsbMidi();
    Tick(100);
    std::size_t top = 99;
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        if(piano.MidiVoiceNote(i) == 127) top = kNumKeys + i;
    assert(top != 99 && piano.VoiceHarmonic(top) == 0.0f);                 // would alias
    assert(std::fabs(piano.VoiceHarmonic(0) - 0.5f) < 1e-5f); // 0.12 + 0.5, clamped to the 0.5 maximum
    Request(mod::proto::ClearMatrix, {});
    Tick(2);
    assert(piano.VoiceHarmonic(0) == toy_piano::kSecondHarmonic && piano.VoiceHarmonic(top) == 0.0f);
}

// v0.4: Studio One input monitoring sends everything from the Daisy back to it (hardware
// test of 24.10.2026 with Physical Modeling). Echoed replies, NAKs and monitor frames are
// consumed without an answer, so no NAK loop can drown the editor's replies.
void TestEchoedReplies() {
    Request(mod::proto::Identify, {});
    const std::vector<uint8_t> reply = usb_midi.sent[0];
    usb_midi.sent.clear();
    Receive(reply.data(), reply.size()); // the reply comes back as if it were a request
    ServiceUsbMidi();
    ++daisy::System::now;
    assert(usb_midi.sent.empty() && modulation.controller.IgnoredReplies() == 1);

    Request(mod::proto::Monitor, {20});
    unsigned frames = 0, naks = 0;
    auto run = [&](unsigned ms, bool echo) {
        for(unsigned i = 0; i < ms; ++i) {
            Tick();
            usb_midi.sent.clear();
            ServiceUsbMidi();
            for(const std::vector<uint8_t>& out : usb_midi.sent) {
                mod::proto::Frame f;
                assert(mod::proto::ParseHeader(out.data() + 1, out.size() - 2, f) == mod::proto::Parse::Ok);
                ++frames;
                naks += f.command == mod::proto::kNak;
                if(echo)
                    Receive(out.data(), out.size());
            }
        }
    };
    run(1000, true);
    std::printf("echoed replies: %u monitor frames in 1 s, %u NAK\n", frames, naks);
    assert(naks == 0 && frames >= 19 && frames <= 21); // 20 Hz monitor, nothing else
    uint8_t stop[mod::proto::kMaxFrame];
    const uint8_t off = 0;
    Receive(stop, mod::proto::BuildFrame(stop, 0, mod::proto::Monitor, 1, &off, 1));
    run(5, false);
    assert(naks == 0);
    uint8_t command = 0;
    Request(mod::proto::Identify, {}, &command); // requests are still answered
    assert(command == (mod::proto::Identify | mod::proto::kReplyBit));
}
}

int main() {
    hw.Init();
    piano.Init();
    InitModulation(48000.0f);
    hw.pot[1].value = 1.0f; // 3 s decay: notes keep sounding during each test
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(10);
    TestIdentifyAndColumns();
    TestLfoVibrato();
    TestModWheelLevelAndPolyAt();
    TestLatchedAttack();
    TestHarmonicAndAliasRule();
    TestEchoedReplies();
    std::printf("PASS: Toy Piano modulation through the production callback and USB-MIDI\n");
}
