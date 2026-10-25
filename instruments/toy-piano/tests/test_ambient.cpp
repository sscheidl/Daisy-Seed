// MIT. Exercise the actual composed callback, control layout and MIDI protocol.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

static bool in_audio = false;
static unsigned allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t n) {
    if(in_audio) ++allocations;
    if(void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

float left[toy_piano::kBlockSize]{}, right[toy_piano::kBlockSize]{};
void Tick(unsigned count = 1) {
    float* output[]{left, right};
    for(unsigned block = 0; block < count; ++block) {
        in_audio = true;
        AudioCallback(nullptr, output, toy_piano::kBlockSize);
        in_audio = false;
        ++daisy::System::now;
        for(unsigned i = 0; i < toy_piano::kBlockSize; ++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            assert(std::fabs(left[i]) <= .981f && std::fabs(right[i]) <= .981f);
        }
    }
}

daisy::MidiEvent Event(daisy::MidiMessageType type, uint8_t a, uint8_t b) {
    daisy::MidiEvent e;
    e.type = type; e.data[0] = a; e.data[1] = b;
    return e;
}

std::vector<uint8_t> last_reply;
std::vector<uint8_t> Request(uint8_t command, const std::vector<uint8_t>& payload = {}) {
    static uint8_t seq = 0;
    uint8_t bytes[mod::proto::kMaxFrame];
    const auto n = mod::proto::BuildFrame(bytes, 0, command, ++seq & 127, payload.data(), payload.size());
    daisy::MidiEvent e;
    e.type = daisy::SystemCommon; e.sc_type = daisy::SystemExclusive;
    e.sysex_message_len = uint8_t(n - 2);
    std::memcpy(e.sysex_data, bytes + 1, n - 2);
    usb_midi.incoming.push_back(e);
    usb_midi.sent.clear();
    ServiceUsbMidi();
    ++daisy::System::now;
    for(const auto& reply : usb_midi.sent) {
        if(reply.empty() || reply[0] != 0xf0) continue;
        mod::proto::Frame f;
        assert(mod::proto::ParseHeader(reply.data() + 1, reply.size() - 2, f) == mod::proto::Parse::Ok);
        assert(mod::proto::Verify(reply.data() + 1, reply.size() - 2, f) == 0);
        if(f.command == (command | mod::proto::kReplyBit)) {
            last_reply = reply;
            return {f.payload, f.payload + f.length};
        }
    }
    assert(false && "No protocol reply");
    return {};
}

void TestDryReference() {
    toy_piano::ToyPiano reference;
    reference.Init();
    hw.pressed.fill(true);
    uint32_t previous = 0;
    double energy = 0;
    for(unsigned block = 0; block < 250; ++block) {
        Tick();
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key)))
                reference.Trigger(key, toy_piano::kAttackMin, toy_piano::kDecayMin);
        previous = held;
        for(unsigned i = 0; i < toy_piano::kBlockSize; ++i) {
            const float expected = reference.Process();
            assert(left[i] == expected && right[i] == expected);
            energy += double(expected) * expected;
        }
    }
    assert(led_mask.load() == 0xffff && energy > .01);
    hw.pressed.fill(false); Tick(10);
    assert(led_mask.load() == 0);
    std::puts("PASS: all 16 key voices/LED states, fixed attack, VR2 decay and exact dry piano reference");
}

void TestControlsAndWet() {
    for(unsigned bank = 0; bank < 2; ++bank)
        for(unsigned pos = 0; pos < 3; ++pos) {
            hw.tog_sw17.value = bank;
            hw.tog_sw18.value = pos == 0 ? 1 : pos == 1 ? 0 : 2;
            Tick(20);
            assert(fx->SelectedMode() == bank * 3 + pos);
        }
    hw.pot[1].value = 1; Tick(300);
    hw.pressed[0] = true; Tick(10);
    assert(piano.Envelope(0).Active() && fx->SelectedMode() == 5);
    for(float knob : {0.f, .5f, 1.f}) {
        hw.pot[0].value = knob;
        Tick(300);
        const float ratio = piano.VoiceIncrement(0) / piano.BaseIncrement(0);
        const float expected = std::exp2(2 * knob - 1);
        assert(std::fabs(ratio - expected) < .005f);
    }
    hw.pressed[0] = false; Tick(10);
    usb_midi.incoming.push_back(Event(daisy::NoteOn, 127, 100));
    ServiceUsbMidi(); Tick(4);
    bool found = false;
    for(unsigned v = 0; v < 16; ++v) if(piano.MidiVoiceNote(v) == 127) {
        found = true;
        assert(piano.VoiceIncrement(16 + v) < .5f);
        assert(piano.VoiceLevel(16 + v) == 0);
    }
    assert(found);
    hw.pot[0].value = .5f; hw.pot[1].value = 0; hw.pot[5].value = 1;
    hw.tog_sw17.value = false; hw.tog_sw18.value = 1;
    Tick(3500); // Previously sounding long piano voices are finished.
    hw.pressed[0] = true; Tick(12); hw.pressed[0] = false;
    Tick(100);
    assert(!piano.Envelope(0).Active());
    double tail = 0, stereo = 0;
    for(unsigned block = 0; block < 500; ++block) {
        Tick();
        for(unsigned i = 0; i < toy_piano::kBlockSize; ++i) {
            tail += double(left[i]) * left[i] + double(right[i]) * right[i];
            const double difference = left[i] - right[i]; stereo += difference * difference;
        }
    }
    assert(tail > 1e-8 && stereo > 1e-10);
    std::printf("PASS: six switch states, continuous transpose, Nyquist protection, stereo reverb after piano end (%.8f)\n", tail);
}

void TestMidiAndProtocol() {
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    auto response = Request(mod::proto::Identify);
    mod::proto::Reader reader(response.data(), response.size());
    for(unsigned i = 0; i < 5; ++i) reader.U7();
    char key[25]{}, version[13]{};
    reader.Str(key, 24); reader.Str(version, 12);
    assert(std::strcmp(key, "toy-piano-ambient") == 0);
    assert(std::strcmp(version, TOY_PIANO_AMBIENT_VERSION) == 0);
    daisy::MidiEvent echo;
    echo.type = daisy::SystemCommon; echo.sc_type = daisy::SystemExclusive;
    echo.sysex_message_len = uint8_t(last_reply.size() - 2);
    std::memcpy(echo.sysex_data, last_reply.data() + 1, echo.sysex_message_len);
    usb_midi.incoming.push_back(echo); ServiceUsbMidi();
    assert(modulation.controller.IgnoredReplies() == 1 && modulation.controller.PendingReplies() == 0);
    for(uint8_t t = 0; t < 8; ++t) assert(Request(mod::proto::GetTarget, {t})[1] & 1);
    assert(Request(mod::proto::GetMode, {5})[1] == 6);
    assert(!Request(mod::proto::GetStats).empty());
    NoteIn note{};
    while(midi_in.Pop(note)) {}
    for(unsigned i = 0; i < 100; ++i)
        usb_midi.incoming.push_back(Event(daisy::NoteOn, uint8_t(36 + i % 60), 70));
    ServiceUsbMidi();
    assert(!usb_midi.incoming.empty()); // Bounded main-loop batch.
    ServiceUsbMidi(); // Queue full: retain one popped Note On for retry.
    std::vector<unsigned> received;
    for(unsigned batch = 0; batch < 10; ++batch) {
        while(midi_in.Pop(note)) received.push_back(note.note);
        ServiceUsbMidi(); ++daisy::System::now;
    }
    assert(received.size() == 100);
    for(unsigned i = 0; i < received.size(); ++i) assert(received[i] == 36 + i % 60);
    std::puts("PASS: USB protocol identity, 8 dynamic editor targets, mode metadata, CPU stats, echo guard and bounded Note On backpressure");
}

void TestShimmerCallback() {
    // Sixth switch state must now render a wet tail, not the previous Dry placeholder.
    hw.tog_sw17.value = true; hw.tog_sw18.value = 2;
    hw.pot[1].value = 0; hw.pot[2].value = 1; hw.pot[5].value = 1;
    hw.pot[6].value = 1;
    Tick(400);
    assert(fx->SelectedMode() == 5);
    assert(std::strcmp(toy_piano::kFxModes[5].name, "Shimmer") == 0);
    for(float stretch : {0.f, 1.f}) {
        hw.pot[7].value = stretch; Tick(400);
        hw.pressed[0] = true; Tick(12); hw.pressed[0] = false;
        Tick(100);
        assert(!piano.Envelope(0).Active());
        double energy = 0;
        for(unsigned b = 0; b < 1200; ++b) {
            Tick();
            for(unsigned i = 0; i < toy_piano::kBlockSize; ++i)
                energy += double(left[i]) * left[i] + double(right[i]) * right[i];
        }
        assert(energy > 1e-7);
    }
    std::puts("PASS: sixth-mode Shimmer stereo tail at both Stretch endpoints through production callback");
}

void TestMeter() {
    mod::adapter::CallbackDiagnostics meter;
    meter.Init(48000, 48);
    daisy::System::ticks = UINT32_MAX - 200;
    meter.Begin(); daisy::System::ticks += 500; meter.End();
    auto stats = meter.GetStats();
    assert(stats.ticks == 1 && stats.overruns == 0 && std::abs(int(stats.load_max) - 5000) < 3);
    meter.ResetStats();
    meter.Begin(); daisy::System::ticks += 1200; meter.End();
    stats = meter.GetStats();
    assert(stats.ticks == 1 && stats.overruns == 1 && stats.load_max >= 11999);
    std::puts("PASS: CPU snapshot coherence, reset, timer wrap and loads above 100 percent (fake clock only)");
}

int main() {
    hw.Init(); piano.Init(); assert(InitializeFx()); InitModulation(48000);
    for(auto& pot : hw.pot) pot.value = .5f;
    hw.pot[1].value = 0; hw.pot[5].value = 0; hw.pot[7].value = 0;
    TestDryReference();
    TestControlsAndWet();
    TestMidiAndProtocol();
    TestShimmerCallback();
    TestMeter();
    assert(allocations == 0);
    std::puts("PASS composed Toy Piano + Ambient; no allocation in the audio callback");
}
