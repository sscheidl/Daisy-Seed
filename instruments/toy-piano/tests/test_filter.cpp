// MIT. End-to-end test of toy-piano-filter: the production callback, the two control pages,
// the matrix columns, the load test and the diagnostics path.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
double energy = 0, high = 0, previous = 0;
void Tick(unsigned count = 1) {
    float* output[]{left, right};
    for(unsigned block = 0; block < count; ++block) {
        in_audio = true;
        AudioCallback(nullptr, output, kN);
        in_audio = false;
        ++daisy::System::now;
        for(unsigned i = 0; i < kN; ++i) {
            assert(std::isfinite(left[i]) && left[i] == right[i] && std::fabs(left[i]) <= 1.f);
            energy += double(left[i]) * left[i];
            const double d = left[i] - previous; // a first difference: weighs the highs
            high += d * d;
            previous = left[i];
        }
    }
}
void Tap(unsigned key) {
    hw.pressed[key] = true;
    Tick(20);
    hw.pressed[key] = false;
}
// Plays a key and returns the energy of the first 300 ms, and the share of highs in `highs`.
double Play(unsigned key, double* highs = nullptr) {
    Tick(800); // the previous note and its tail end
    energy = high = 0;
    Tap(key);
    Tick(280);
    if(highs) *highs = high / (energy + 1e-20);
    return energy;
}
void Pots(float wave, float attack, float decay, float cutoff, float resonance, float drive, float age) {
    const float pots[8] = {.5f, wave, attack, decay, cutoff, resonance, drive, age};
    for(unsigned i = 0; i < 8; ++i) hw.pot[i].value = pots[i];
    Tick(400); // the 20 ms pot filter
}
const toy_piano::FilterControls& Panel() { return filter_host->Controls(); }

void TestPotsAndModes() {
    hw.tog_sw17.value = false;
    hw.tog_sw18.value = toy_piano::kSwitchUp;
    Pots(1.f, 0.f, .8f, 1.f, 0.f, 0.f, 0.f);
    assert(Panel().mode == toy_piano::filter::Mode::Lp24 && !Panel().page2);
    assert(Panel().cutoff > .99f && Panel().resonance == 0.f && Panel().drive == 0.f && Panel().age == 0.f);
    double open_highs = 0, closed_highs = 0;
    const double open = Play(4, &open_highs); // G4 saw through the open filter
    assert(open > 1e-3);
    Pots(1.f, 0.f, .8f, .35f, 0.f, 0.f, 0.f); // cutoff near the note
    Play(4, &closed_highs);
    assert(closed_highs < open_highs * .5);
    // SW18: LP, BP, HP. A low cutoff under HP leaves the highs.
    hw.tog_sw18.value = toy_piano::kSwitchDown;
    Tick(5);
    assert(Panel().mode == toy_piano::filter::Mode::Hp);
    double hp_highs = 0;
    Play(4, &hp_highs);
    assert(hp_highs > closed_highs);
    hw.tog_sw18.value = 0;
    Tick(5);
    assert(Panel().mode == toy_piano::filter::Mode::Bp);
    hw.tog_sw18.value = toy_piano::kSwitchUp;
    Tick(5);
}

void TestSharedVoicesAndPickUp() {
    Pots(.5f, 0.f, .8f, .6f, .4f, .3f, 0.f);
    const float cutoff = Panel().cutoff, resonance = Panel().resonance, drive = Panel().drive;
    // Page 2: VR5-VR7 change meaning. Nothing moves until a pot reaches its stored value.
    hw.tog_sw17.value = true;
    Tick(2);
    assert(Panel().page2);
    assert(Panel().env == 0.f && Panel().model == toy_piano::filter::Model::Stages && !Panel().shared && Panel().load_voices == 0);
    Tick(50);
    assert(Panel().env == 0.f && Panel().load_voices == 0); // pots sit elsewhere: no jump
    hw.pot[4].value = .5f; // reach the centre: picked up
    Tick(100);
    hw.pot[4].value = 1.f;
    Tick(200);
    assert(Panel().env > .98f);
    assert(Panel().cutoff == cutoff && Panel().resonance == resonance && Panel().drive == drive); // page 1 kept
    // Back on page 1 the same: page 2 kept, nothing jumps.
    const float env = Panel().env;
    hw.tog_sw17.value = false;
    Tick(100);
    assert(Panel().cutoff == cutoff && Panel().env == env);
    hw.pot[4].value = cutoff;
    Tick(200);
    hw.pot[4].value = .4f;
    Tick(200);
    assert(std::fabs(Panel().cutoff - .4f) < .01f);
}

// The envelope moves the cutoff of one voice only, through the per-voice cutoff input.
void TestEnvelope() {
    hw.tog_sw17.value = false;
    Pots(0.f, 0.f, .5f, .4f, 0.f, 0.f, 0.f);
    Tick(400);
    Tap(0);
    Tick(5);
    const float with_env = filter_host->VoiceCutoff(0);
    assert(with_env > 24.f + 111.f * .4f + 30.f); // env was left at +1, at the start of the decay
    Tick(1000);
    assert(filter_host->VoiceCutoff(0) < with_env - 20.f);
    assert(filter_host->VoiceCutoff(5) < with_env); // a silent voice follows nothing
}

void TestLoadTest() {
    hw.tog_sw17.value = true;
    hw.pot[4].value = .5f;
    Tick(50);
    // VR7: pick up at the left, then step through 4 .. 32 voices.
    hw.pot[6].value = 0.f;
    Tick(100);
    for(unsigned step = 0; step < toy_piano::kLoadSteps; ++step) {
        hw.pot[6].value = (float(step) + .5f) / float(toy_piano::kLoadSteps);
        Tick(100);
        assert(Panel().load_voices == 4 * step);
        Tick(4000); // long enough to see voices re-strike
        if(step) {
            assert(filter_host->ActiveFilters() >= 4 * step);
            assert((LedMask() & 0xFFu) == (1u << step) - 1u);
        }
    }
    assert(filter_host->ActiveFilters() == toy_piano::kNumVoices);
    // VR6: four models, the fourth is one shared filter on the sum.
    using M = toy_piano::filter::Model;
    hw.pot[5].value = .1f;
    Tick(300);
    hw.pot[5].value = .1f;
    Tick(50);
    if(Panel().model != M::Clean) { // pick-up: sweep through the stored value
        hw.pot[5].value = .6f;
        Tick(200);
        hw.pot[5].value = .1f;
        Tick(200);
    }
    assert(Panel().model == M::Clean && !Panel().shared);
    hw.pot[5].value = .4f;
    Tick(200);
    assert(Panel().model == M::Sum);
    hw.pot[5].value = .9f;
    Tick(200);
    assert(Panel().shared);
    energy = 0;
    Tick(200);
    assert(energy > 0);
    hw.pot[5].value = .6f;
    hw.pot[6].value = 0.f;
    Tick(300);
    assert(Panel().model == M::Stages && !Panel().shared && Panel().load_voices == 0);
    hw.tog_sw17.value = false;
    Tick(5);
    assert(LedMask() == led_mask.load());
}

// Everything at the top at once on all voices.
void TestExtremes() {
    hw.tog_sw17.value = true;
    hw.pot[6].value = 1.f;
    Tick(300);
    hw.tog_sw17.value = false;
    Pots(1.f, 0.f, 1.f, .6f, 0.f, 0.f, 1.f); // VR6 and VR7 pick up at their stored 0
    Pots(1.f, 0.f, 1.f, .6f, 1.f, 1.f, 1.f);
    assert(Panel().resonance == 1.f && Panel().drive == 1.f && Panel().age == 1.f && Panel().load_voices == 32);
    Tick(300);
    for(unsigned i = 0; i < 3; ++i) { // all three filter types
        hw.tog_sw18.value = i == 0 ? toy_piano::kSwitchUp : (i == 1 ? 0 : toy_piano::kSwitchDown);
        energy = 0;
        Tick(2000);
        assert(energy > 0);
    }
    hw.tog_sw18.value = toy_piano::kSwitchUp;
}

void TestMatrixColumns() {
    const mod::InstrumentInfo info = toy_piano::MakeModInfo();
    assert(!std::strcmp(info.key, "toy-piano-filter"));
    assert(!std::strcmp(info.targets[toy_piano::ModCutoff].name, "Cutoff") && info.targets[toy_piano::ModCutoff].per_voice);
    assert(!std::strcmp(info.targets[toy_piano::ModResonance].name, "Resonance"));
    assert(!std::strcmp(info.targets[toy_piano::ModDrive].name, "Drive"));
}

void TestBootloader() {
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    for(unsigned i = 0; i < 5100; ++i) {
        Tick();
        ServiceBootloader();
    }
    assert(daisy::System::bootloader_resets == resets + 1);
    hw.pressed[8] = hw.pressed[15] = false;
}

int main() {
    hw.Init();
    piano.Init(48000.f);
    InitializeFilter();
    InitModulation(48000.f);
    for(auto& p : hw.pot) p.value = 0.f;
    Tick(50);
    TestPotsAndModes();
    TestSharedVoicesAndPickUp();
    TestEnvelope();
    TestLoadTest();
    TestExtremes();
    TestMatrixColumns();
    TestBootloader();
    assert(allocations == 0);
    std::puts("PASS: toy-piano-filter: pots, SW18 types, two pages with pick-up, per-voice envelope, load test, "
              "shared comparison, LEDs, matrix columns, extremes, bootloader, no allocation");
    return 0;
}
