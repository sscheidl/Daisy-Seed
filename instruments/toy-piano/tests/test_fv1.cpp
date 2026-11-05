// MIT. End-to-end test of toy-piano-fv1 (research / feasibility study): the production callback,
// pot mapping, MIDI program selection and the CPU telemetry packet, with the own demo programs.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

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
// Reference VR1/VR2 filters, fed with the same pot values as the firmware in every block.
toy_piano::PotFilter ref_pitch_pot, ref_attack_pot, ref_decay_pot;
float ref_pitch = .5f;
float ref_attack = 0, ref_decay = 0;
float RefAttack() { return toy_piano::LogTime(ref_attack, toy_piano::kAttackMin, toy_piano::kAttackMax); }
float RefDecay() { return toy_piano::LogTime(ref_decay, toy_piano::kDecayMin, toy_piano::kDecayMax); }
void Tick(unsigned count = 1) {
    float* output[]{left, right};
    for(unsigned block = 0; block < count; ++block) {
        ref_pitch = ref_pitch_pot.Process(hw.pot[0].value);
        ref_attack = ref_attack_pot.Process(hw.pot[1].value);
        ref_decay = ref_decay_pot.Process(hw.pot[2].value);
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
void SendCc(unsigned channel, unsigned cc, unsigned value) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange; e.channel = int(channel);
    e.data[0] = uint8_t(cc); e.data[1] = uint8_t(value);
    usb_midi.incoming.push_back(e);
}

void SendProgramChange(unsigned channel, unsigned program) {
    daisy::MidiEvent e;
    e.type = daisy::ProgramChange; e.channel = int(channel);
    e.data[0] = uint8_t(program);
    usb_midi.incoming.push_back(e);
}

// Keys -> piano (VR1 attack, VR2 decay) -> Fv1Host (program 1 = own echo; VR3..VR5, VR6 mix) -> output,
// bit-identical to the components.
void TestExactComposition() {
    const float pots[8] = {.5f, .3f, .55f, .2f, .6f, .9f, .7f, 0.f}; // VR1 centre, VR8 left = 32 kHz
    for(unsigned i = 0; i < 8; ++i) hw.pot[i].value = pots[i];
    SendCc(15, 22, 0); SendCc(15, 23, 1); // program index 1 (echo)
    ServiceUsbMidi();
    toy_piano::ToyPiano reference;
    reference.Init();
    auto host = std::make_unique<toy_piano::Fv1Host>();
    assert(host->Prepare({}));
    std::array<float, 8> raw{};
    for(unsigned i = 0; i < 8; ++i) raw[i] = pots[i];
    uint32_t previous = 0;
    double energy = 0, wet_difference = 0;
    const unsigned pattern[] = {0, 4, 7, 9, 15, 2, 11};
    for(unsigned block = 0; block < 2500; ++block) {
        const unsigned step = block / 200;
        for(unsigned k = 0; k < 16; ++k) hw.pressed[k] = false;
        if(step < std::size(pattern) && block % 200 < 30) hw.pressed[pattern[step]] = true;
        Tick();
        host->ReadControls(raw, false);
        if(block == 0) host->Select(1);
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key)))
                reference.Trigger(key, RefAttack(), RefDecay());
        previous = held;
        float mono[kN], l[kN], r[kN];
        for(unsigned i = 0; i < kN; ++i) mono[i] = reference.Process();
        assert(host->Process(mono, l, r, kN));
        for(unsigned i = 0; i < kN; ++i) {
            assert(left[i] == l[i] && right[i] == r[i]);
            energy += double(l[i]) * l[i];
            wet_difference += std::fabs(l[i] - mono[i]);
        }
    }
    assert(fv1->Program() == 1 && energy > 1 && wet_difference > 10);
    std::puts("PASS: Toy Piano (VR2/VR3) -> fv1-vm (echo, VR4..VR6, mix VR7 0.7, VR8 32 kHz) -> output bit-identical to the components");
}

// Exact dry piano with VR6 = 0 and with SW17 bypass; VR1/VR2 still set the piano times.
void TestDryMixAndBypass() {
    for(unsigned mode = 0; mode < 2; ++mode) {
        hw.pressed.fill(false);
        Tick(3000);
        hw.pot[1].value = mode ? .8f : .1f; // VR2 attack
        hw.pot[2].value = mode ? .2f : .9f; // VR3 decay
        hw.pot[6].value = mode ? 1.f : 0.f; // VR7: dry only, or full wet behind the bypass
        hw.tog_sw17.value = mode == 1;
        Tick(50);
        assert(fv1->Bypass() == (mode == 1));
        piano.Init();
        toy_piano::ToyPiano reference;
        reference.Init();
        hw.pressed.fill(true);
        uint32_t previous = 0;
        double energy = 0;
        for(unsigned block = 0; block < 200; ++block) {
            Tick();
            const uint32_t held = led_mask.load();
            for(unsigned key = 0; key < 16; ++key)
                if((held & (1u << key)) && !(previous & (1u << key)))
                    reference.Trigger(key, RefAttack(), RefDecay());
            previous = held;
            for(unsigned i = 0; i < kN; ++i) {
                const float expected = reference.Process();
                assert(left[i] == expected && right[i] == expected);
                energy += double(expected) * expected;
            }
        }
        assert(energy > 1e-3);
        hw.pressed.fill(false);
        Tick(10);
    }
    hw.tog_sw17.value = false;
    hw.pot[6].value = 0.f;
    Tick(2);
    std::puts("PASS: VR7 = 0 and SW17 bypass give the exact dry Toy Piano with VR2/VR3 attack and decay");
}

void TestTelemetryAndProgramChange() {
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    fv1_meter.Init((SystemCoreClock / 48000u) * uint32_t(kN)); // as main()
    fakeDwt.CYCCNT.step = 96000; // 20 % of the 480 000-cycle block budget per callback
    SendCc(15, 119, 2);
    ServiceUsbMidi();
    Tick(5);
    usb_midi.sent.clear();
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2 && usb_midi.sent[0].size() == 60 && usb_midi.sent[1].size() == 30);
    {   // status packet: rate 32 kHz (VR8 left), transposition 0 (VR1 centre), mix and bypass as set
        const auto& st = usb_midi.sent[1];
        auto v = [&](unsigned cc) { return unsigned(st[3 * (cc - 80) + 2]); };
        for(unsigned i = 0; i < 10; ++i) assert(st[3 * i] == 0xBF && st[3 * i + 1] == 80 + i);
        assert(v(80) == 1 && v(89) == 64 && (v(81) | v(82) << 7) == 3200 && (v(83) | v(84) << 7) == 2400);
        assert(v(85) == unsigned(std::lround(fv1->Mix() * 100)) && v(86) == (fv1->Bypass() ? 1u : 0u));
    }
    const auto& p = usb_midi.sent[0];
    auto value = [&](unsigned cc) { return unsigned(p[3 * (cc - 100) + 2]); };
    for(unsigned i = 0; i < 20; ++i) assert(p[3 * i] == 0xBF && p[3 * i + 1] == 100 + i);
    const unsigned peak = value(104) | value(105) << 7 | value(106) << 14;
    const unsigned blocks = value(107) | value(108) << 7;
    assert(value(100) == 2 && value(119) == 64 && peak == 2000 && blocks == 1 && value(116) == 1);
    // Program change over MIDI: the block is muted and not measured; the index wraps.
    SendCc(15, 22, 0); SendCc(15, 23, 2);
    ServiceUsbMidi();
    Tick();
    assert(fv1->Program() == 0 && fv1->Count() == fv1_fw::kBuiltinCount);
    for(unsigned i = 0; i < kN; ++i) assert(left[i] == 0.f && right[i] == 0.f);
    SendCc(15, 119, 1);
    ServiceUsbMidi();
    Tick(3);
    usb_midi.sent.clear();
    ServiceUsbMidi();
    assert(usb_midi.sent.size() == 2);
    const unsigned blocks2 = usb_midi.sent[0][3 * 7 + 2];
    assert(blocks2 == 1); // only the block after the change
    fakeDwt.CYCCNT.step = 0;
    std::puts("PASS: CC119 telemetry (peak 20.00 %, blocks) and CC22/23 program change (muted, unmeasured)");
}

// Standard Program Change from a keyboard on channel 1, with Bank Select; notes keep working.
void TestStandardProgramChange() {
    SendProgramChange(0, 1);
    ServiceUsbMidi();
    Tick();
    assert(fv1->Program() == 1);
    SendCc(0, 0, 0); SendCc(0, 32, 1); SendProgramChange(0, 0); // bank 1 -> index 128, wraps over 2 programs
    ServiceUsbMidi();
    Tick();
    assert(fv1->Program() == 0);
    SendCc(0, 32, 0);
    daisy::MidiEvent note;
    note.type = daisy::NoteOn; note.channel = 0; note.data[0] = 60; note.data[1] = 100;
    usb_midi.incoming.push_back(note);
    ServiceUsbMidi();
    hw.pot[6].value = 0.f; // VR7 dry: the note must sound
    double energy = 0;
    for(unsigned b = 0; b < 50; ++b) {
        Tick();
        for(unsigned i = 0; i < kN; ++i) energy += double(left[i]) * left[i];
    }
    assert(energy > 1e-3);
    std::puts("PASS: Program Change (any channel) with Bank Select CC0/CC32; notes still play");
}

// VR1 transposes all voices (+24 semitones at the right stop) bit-exactly like ToyPiano::Modulate;
// VR8 sets the interpreter rate (32 kHz left, 16 kHz centre, 8 kHz right).
void TestTransposeAndRate() {
    hw.pressed.fill(false);
    hw.tog_sw17.value = false;
    hw.pot[6].value = 0.f; // dry: compare the piano
    hw.pot[0].value = 1.f;
    Tick(3000);
    piano.Init();
    toy_piano::ToyPiano reference;
    reference.Init();
    hw.pressed[3] = true;
    uint32_t previous = 0;
    for(unsigned block = 0; block < 300; ++block) {
        if(block == 20) hw.pressed[3] = false;
        Tick();
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key))) reference.Trigger(key, RefAttack(), RefDecay());
        previous = held;
        const float transpose = std::fabs(ref_pitch - .5f) < .005f ? 1.f : std::exp2((ref_pitch - .5f) * 4.f);
        for(size_t v = 0; v < toy_piano::kNumVoices; ++v) {
            float ratio = transpose, gain = 1.f;
            if(reference.BaseIncrement(v) * ratio >= .5f) { ratio = .49f / reference.BaseIncrement(v); gain = 0; }
            reference.Modulate(v, ratio, gain, toy_piano::HarmonicWeight(toy_piano::kHarmonicBase), kN);
        }
        for(unsigned i = 0; i < kN; ++i) {
            const float expected = reference.Process();
            assert(left[i] == expected && right[i] == expected);
        }
    }
    assert(std::fabs(ref_pitch - 1.f) < 1e-3f && piano_transpose > 3.99f);
    for(float x : {0.f, .5f, 1.f}) {
        hw.pot[7].value = x;
        Tick(100);
        const float want = 32000.f * std::exp2(-2.f * x);
        assert(std::fabs(fv1->Rate() - want) < 1.f);
    }
    hw.pot[0].value = .5f; hw.pot[7].value = 0.f;
    Tick(100);
    assert(piano_transpose == 1.f);
    std::puts("PASS: VR1 +24 semitones bit-exact to ToyPiano::Modulate; VR8 rate 32/16/8 kHz; centre detent restores pitch");
}

// DIN MIDI: notes and Program Change work like USB; SysEx over DIN is ignored.
void TestDinMidi() {
    hw.pot[6].value = 0.f;
    hw.pressed.fill(false);
    Tick(3400);
    daisy::MidiEvent note;
    note.type = daisy::NoteOn; note.channel = 3; note.data[0] = 72; note.data[1] = 90;
    hw.midi.incoming.push_back(note);
    daisy::MidiEvent pc;
    pc.type = daisy::ProgramChange; pc.channel = 0; pc.data[0] = 1;
    hw.midi.incoming.push_back(pc);
    ServiceUsbMidi();
    Tick();
    assert(fv1->Program() == 1 && hw.midi.incoming.empty());
    double energy = 0;
    for(unsigned b = 0; b < 30; ++b) {
        Tick();
        for(unsigned i = 0; i < kN; ++i) energy += double(left[i]) * left[i];
    }
    assert(energy > 1e-3);
    daisy::MidiEvent sysex;
    sysex.type = daisy::SystemCommon; sysex.sc_type = daisy::SystemExclusive;
    hw.midi.incoming.push_back(sysex);
    ServiceUsbMidi();
    assert(hw.midi.incoming.empty());
    std::puts("PASS: DIN MIDI note and Program Change; SysEx over DIN ignored");
}

int main() {
    hw.Init(); piano.Init(); assert(InitializeFx()); InitModulation(48000);
    assert(std::strcmp(toy_piano::MakeModInfo().key, "toy-piano-fv1") == 0);
    TestExactComposition();
    TestDryMixAndBypass();
    TestTelemetryAndProgramChange();
    TestStandardProgramChange();
    TestTransposeAndRate();
    TestDinMidi();
    assert(allocations == 0);
    std::puts("PASS composed Toy Piano + fv1-vm; no allocation in the audio callback");
}
