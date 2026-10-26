// MIT. End-to-end test of toy-piano-tape: the production callback, controls and protocol.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
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

constexpr std::size_t kN = toy_piano::kBlockSize;
float left[kN]{}, right[kN]{};
void Tick(unsigned count = 1) {
    float* output[]{left, right};
    for(unsigned block = 0; block < count; ++block) {
        in_audio = true;
        AudioCallback(nullptr, output, kN);
        in_audio = false;
        ++daisy::System::now;
        for(unsigned i = 0; i < kN; ++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            assert(std::fabs(left[i]) <= 1.f && std::fabs(right[i]) <= 1.f);
        }
    }
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

const float kAttack = toy_piano::kAttackMin;
const float kDecay = toy_piano::LogTime(toy_piano::kTapeDecayBase, toy_piano::kDecayMin, toy_piano::kDecayMax);

// Signal path Toy Piano -> Tape FX -> output, bit for bit against the separately
// composed components (reference piano, TapeFx, TapeMixer with the same settings).
void TestExactComposition() {
    // VR1..VR8 = Drive, Tone, Age, Mix, Wow, Flutter, Hiss, Dropout.
    const float pots[8] = {.7f, .6f, .8f, 1.f, 1.f, .8f, .3f, .5f};
    for(unsigned i = 0; i < 8; ++i) hw.pot[i].value = pots[i];
    hw.tog_sw17.value = false;           // Tape on
    hw.tog_sw18.value = seed::fx::TapePanel::kSwitchDown; // Cassette, after 15 ms
    toy_piano::ToyPiano reference;
    reference.Init();
    seed::fx::TapePanel panel;
    panel.Reset();
    seed::fx::TapeMixer mixer;
    mixer.Reset(panel.Mix(), false);
    auto storage = std::make_unique<seed::fx::TapeFx::Storage>();
    seed::fx::TapeFx effect;
    effect.SetParameters(panel.Apply({}));
    assert(effect.Prepare({48000, kN}, *storage));
    std::array<float, 8> raw{};
    for(unsigned i = 0; i < 8; ++i) raw[i] = pots[i];
    uint32_t previous = 0;
    double energy = 0, wet_difference = 0;
    const unsigned pattern[] = {0, 4, 7, 9, 15, 2, 11};
    for(unsigned block = 0; block < 3000; ++block) {
        const unsigned step = block / 200;
        for(unsigned k = 0; k < 16; ++k) hw.pressed[k] = false;
        if(step < std::size(pattern) && block % 200 < 30) hw.pressed[pattern[step]] = true;
        Tick();
        if(panel.Update(raw, false, hw.tog_sw18.value)) effect.SetParameters(panel.Apply({}));
        mixer.SetMix(panel.Mix());
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key)))
                reference.Trigger(key, kAttack, kDecay);
        previous = held;
        float mono[kN], l[kN], r[kN];
        for(unsigned i = 0; i < kN; ++i) mono[i] = reference.Process();
        assert(effect.Process({mono, mono, l, r, kN}));
        mixer.Process(mono, mono, l, r, kN);
        for(unsigned i = 0; i < kN; ++i) {
            assert(left[i] == l[i] && right[i] == r[i]);
            energy += double(l[i]) * l[i];
            wet_difference += std::fabs(l[i] - mono[i]);
        }
    }
    assert(energy > 1 && wet_difference > 10);
    assert(tape->Panel().Character() == seed::fx::tape::Character::Cassette);
    assert(tape->Effect().Requested().drive == .7f && tape->Effect().Requested().wow == 1.f);
    std::printf("PASS 10: Toy Piano -> Tape FX -> output bit-identical to the composed components (60 s, 7 keys, Cassette)\n");
}

void TestBypassAndKeys() {
    hw.pressed.fill(false);
    Tick(3500); // let earlier voices finish
    hw.tog_sw17.value = true; // bypass
    Tick(20);
    // Oscillator phases survive silent voices, so start both pianos fresh.
    piano.Init();
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
                reference.Trigger(key, kAttack, kDecay);
        previous = held;
        for(unsigned i = 0; i < kN; ++i) {
            const float expected = reference.Process();
            assert(left[i] == expected && right[i] == expected); // exact dry piano
            energy += double(expected) * expected;
        }
    }
    assert(led_mask.load() == 0xffff && energy > .01);
    hw.pressed.fill(false); Tick(10);
    assert(led_mask.load() == 0);
    std::puts("PASS: SW17 bypass gives the exact dry Toy Piano; all 16 keys and LED states");
}

void TestAudibleRanges() {
    // Wow, flutter and drive at their full TapeVibe ranges through the pots.
    hw.tog_sw17.value = false;
    hw.pot[2].value = 1.f; // Age
    hw.pot[4].value = 1.f; hw.pot[5].value = 0.f; // Wow only
    Tick(400);
    float lo = 1e9f, hi = -1e9f;
    for(unsigned b = 0; b < 1300; ++b) {
        Tick();
        lo = std::min(lo, tape->Effect().LastDelaySamples());
        hi = std::max(hi, tape->Effect().LastDelaySamples());
    }
    assert(std::fabs((hi - lo) / 2 - 5.1f) < .01f); // +-5.1 samples at 0.8 Hz (= +-0.9 cent)
    hw.pot[4].value = 0.f; hw.pot[5].value = 1.f; // Flutter only
    Tick(400);
    lo = 1e9f; hi = -1e9f;
    for(unsigned b = 0; b < 1000; ++b) {
        Tick();
        lo = std::min(lo, tape->Effect().LastDelaySamples());
        hi = std::max(hi, tape->Effect().LastDelaySamples());
    }
    assert((hi - lo) / 2 > 1.4f && (hi - lo) / 2 < 1.66f);
    // Drive 0 vs 100 % on the same struck 16-key cluster: the part of the driven output
    // that is not a scaled copy of the clean output (harmonics, compression).
    std::vector<float> y[2];
    for(unsigned d = 0; d < 2; ++d) {
        hw.pot[0].value = d ? 1.f : 0.f;
        hw.pot[4].value = hw.pot[5].value = hw.pot[6].value = hw.pot[7].value = 0;
        hw.pot[2].value = 0; hw.pot[3].value = 1.f;
        hw.pressed.fill(false); Tick(3500);
        piano.Init();
        hw.pressed.fill(true);
        for(unsigned b = 0; b < 300; ++b) {
            if(b == 30) hw.pressed.fill(false);
            Tick();
            y[d].insert(y[d].end(), left, left + kN);
        }
    }
    double c01 = 0, c00 = 0, c11 = 0;
    for(std::size_t i = 0; i < y[0].size(); ++i) {
        c01 += double(y[0][i]) * y[1][i]; c00 += double(y[0][i]) * y[0][i]; c11 += double(y[1][i]) * y[1][i];
    }
    const double distortion = 1 - c01 * c01 / (c00 * c11); // residual energy share
    assert(distortion > .01);
    std::printf("PASS: audible test ranges through the pots (wow +-5.1 samples, flutter, drive 100 %% adds %.1f %% non-linear residual)\n",
                100 * distortion);
}

void TestSwitchAndProtocol() {
    hw.tog_sw18.value = seed::fx::TapePanel::kSwitchUp; Tick(20);
    assert(tape->Panel().Character() == seed::fx::tape::Character::Studio);
    hw.tog_sw18.value = seed::fx::TapePanel::kSwitchCenter; Tick(20);
    assert(tape->Panel().Character() == seed::fx::tape::Character::Tape);
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    auto response = Request(mod::proto::Identify);
    mod::proto::Reader reader(response.data(), response.size());
    for(unsigned i = 0; i < 5; ++i) reader.U7();
    char key[25]{}, version[13]{};
    reader.Str(key, 24); reader.Str(version, 12);
    assert(std::strcmp(key, "toy-piano-tape") == 0);
    assert(std::strcmp(version, TOY_PIANO_TAPE_VERSION) == 0);
    daisy::MidiEvent echo;
    echo.type = daisy::SystemCommon; echo.sc_type = daisy::SystemExclusive;
    echo.sysex_message_len = uint8_t(last_reply.size() - 2);
    std::memcpy(echo.sysex_data, last_reply.data() + 1, echo.sysex_message_len);
    usb_midi.incoming.push_back(echo); ServiceUsbMidi();
    assert(modulation.controller.IgnoredReplies() == 1);
    for(uint8_t t = 0; t < 5; ++t) assert(Request(mod::proto::GetTarget, {t})[1] & 1);
    assert(!Request(mod::proto::GetStats).empty());
    // MIDI notes still reach the piano and pass the tape.
    daisy::MidiEvent e;
    e.type = daisy::NoteOn; e.data[0] = 64; e.data[1] = 100;
    usb_midi.incoming.push_back(e);
    ServiceUsbMidi(); Tick(4);
    bool found = false;
    for(unsigned v = 0; v < 16; ++v) found |= piano.MidiVoiceNote(v) == 64;
    assert(found);
    hUsbDeviceHS.dev_state = 0;
    std::puts("PASS: SW18 character, IDENTIFY toy-piano-tape, echo guard, five matrix targets, CPU stats, MIDI notes");
}

void TestBootloader() {
    hw.pressed.fill(false); Tick(10); ServiceUsbMidi();
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    Tick(4990); ServiceBootloader();
    assert(daisy::System::bootloader_resets == resets);
    Tick(20); ServiceBootloader();
    assert(daisy::System::bootloader_resets == resets + 1);
    hw.pressed.fill(false); Tick(10);
    std::puts("PASS: bootloader keys S9+S16 (5 s) with the tape running");
}

int main() {
    hw.Init(); piano.Init(); assert(InitializeFx()); InitModulation(48000);
    TestExactComposition();
    TestBypassAndKeys();
    TestAudibleRanges();
    TestSwitchAndProtocol();
    TestBootloader();
    assert(allocations == 0);
    std::puts("PASS composed Toy Piano + Tape FX; no allocation in the audio callback");
}
