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
void SendCc(unsigned channel, unsigned cc, unsigned value) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange; e.channel = int(channel);
    e.data[0] = uint8_t(cc); e.data[1] = uint8_t(value);
    usb_midi.incoming.push_back(e);
}

const float kAttack = toy_piano::kAttackMin;
const float kDecay = toy_piano::LogTime(toy_piano::kFv1DecayBase, toy_piano::kDecayMin, toy_piano::kDecayMax);

// Keys -> piano -> Fv1Host (program 1 = own echo) -> output, bit-identical to the components.
void TestExactComposition() {
    const float pots[8] = {.2f, .6f, .9f, .7f, 0.f, 0.f, 0.f, 0.f};
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
        host->ReadControls(raw);
        if(block == 0) host->Select(1);
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key)))
                reference.Trigger(key, kAttack, kDecay);
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
    std::puts("PASS: Toy Piano -> fv1-vm (echo, mix 0.7) -> output bit-identical to the composed components");
}

void TestDryMix() {
    hw.pressed.fill(false);
    Tick(3000);
    hw.pot[3].value = 0.f; // dry only
    Tick(2);
    piano.Init();
    toy_piano::ToyPiano reference;
    reference.Init();
    hw.pressed.fill(true);
    uint32_t previous = 0;
    for(unsigned block = 0; block < 200; ++block) {
        Tick();
        const uint32_t held = led_mask.load();
        for(unsigned key = 0; key < 16; ++key)
            if((held & (1u << key)) && !(previous & (1u << key)))
                reference.Trigger(key, kAttack, kDecay);
        previous = held;
        for(unsigned i = 0; i < kN; ++i) {
            const float expected = reference.Process();
            assert(left[i] == expected && right[i] == expected);
        }
    }
    hw.pressed.fill(false);
    Tick(10);
    std::puts("PASS: VR4 = 0 gives the exact dry Toy Piano");
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
    assert(usb_midi.sent.size() == 1 && usb_midi.sent[0].size() == 60);
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
    assert(usb_midi.sent.size() == 1);
    const unsigned blocks2 = usb_midi.sent[0][3 * 7 + 2];
    assert(blocks2 == 1); // only the block after the change
    fakeDwt.CYCCNT.step = 0;
    std::puts("PASS: CC119 telemetry (peak 20.00 %, blocks) and CC22/23 program change (muted, unmeasured)");
}

int main() {
    hw.Init(); piano.Init(); assert(InitializeFx()); InitModulation(48000);
    assert(std::strcmp(toy_piano::MakeModInfo().key, "toy-piano-fv1") == 0);
    TestExactComposition();
    TestDryMix();
    TestTelemetryAndProgramChange();
    assert(allocations == 0);
    std::puts("PASS composed Toy Piano + fv1-vm; no allocation in the audio callback");
}
