// MIT. The production tape-fx callback with libDaisy's pinned DigitalControl debouncer.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; } // the USB state the firmware reads
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

constexpr unsigned kN = tape_fw::kBlockSize;
static float in_l[kN], in_r[kN], out_l[kN], out_r[kN];
static unsigned sample = 0;

// One callback per 1 ms fake tick; a 220/330 Hz stereo test signal at -6 dBFS.
static void Tick(unsigned count = 1, bool check_dry = false) {
    for(unsigned b = 0; b < count; ++b) {
        for(unsigned i = 0; i < kN; ++i, ++sample) {
            in_l[i] = .5f * std::sin(6.2831853f * 220.f * sample / 48000.f);
            in_r[i] = .5f * std::sin(6.2831853f * 330.f * sample / 48000.f);
        }
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, kN);
        in_audio = false;
        ++daisy::System::now;
        for(unsigned i = 0; i < kN; ++i) {
            assert(std::isfinite(out_l[i]) && std::isfinite(out_r[i]));
            assert(std::fabs(out_l[i]) <= 1.f && std::fabs(out_r[i]) <= 1.f);
            if(check_dry) {
                const float right = hw.right_detect.Pressed() ? in_r[i] : in_l[i];
                assert(out_l[i] == in_l[i] && out_r[i] == right);
            }
        }
    }
}
static void Tap(unsigned key) {
    hw.pressed[key] = true;
    Tick(15);
    hw.pressed[key] = false;
    Tick(10);
}

int main() {
    hw.Init();
    for(auto& p : hw.pot) p.value = .5f;
    hw.right_detect.pressed = true;
    assert(Initialize());
    budget_cycles = 1u << 30;
    Tick(20);
    // Pots: integer percent, drumlogue order; Mix on VR4.
    hw.pot[0].value = 1.f; hw.pot[3].value = 1.f; hw.pot[4].value = 0.f;
    Tick(5);
    assert(tape_fx.Requested().drive == 1.f && tape_fx.Requested().wow == 0.f && panel.Mix() == 1.f);
    // Curve keys S1..S4 are a radio group; S5/S6 toggle compressor and crusher.
    const ::seed::fx::tape::DriveCurve curves[4] = {::seed::fx::tape::DriveCurve::TapeVibe, ::seed::fx::tape::DriveCurve::MixTape,
                                                 ::seed::fx::tape::DriveCurve::Hard, ::seed::fx::tape::DriveCurve::Fold};
    for(unsigned k = 0; k < 4; ++k) {
        Tap(k);
        assert(tape_fx.Requested().curve == curves[k]);
        RenderLeds(ui_state.load());
        for(unsigned led = 0; led < 4; ++led) assert(hw.led[led] == (led == k ? .5f : 0.f));
    }
    Tap(0);
    Tap(4); Tap(5);
    assert(tape_fx.Requested().compressor && tape_fx.Requested().bitcrush);
    assert(tape_fx.Requested().bits == 8 && tape_fx.Requested().crush_stride == 3);
    assert(tape_fx.Requested().threshold_db == -18.f && tape_fx.Requested().ratio == 4.f);
    RenderLeds(ui_state.load());
    assert(hw.led[4] == .5f && hw.led[5] == .5f && hw.led[7] == .5f);
    Tap(4); Tap(5);
    assert(!tape_fx.Requested().compressor && !tape_fx.Requested().bitcrush);
    // Other keys change nothing (S9 + S16 stay the bootloader combination).
    for(unsigned k = 6; k < 16; ++k) Tap(k);
    assert(tape_fx.Requested().curve == ::seed::fx::tape::DriveCurve::TapeVibe && !tape_fx.Requested().compressor);
    // SW18: character of hiss and dropouts after 15 stable control ticks.
    hw.tog_sw18.position = daisy::Switch3::POS_UP; Tick(20);
    assert(tape_fx.Requested().hiss_type == ::seed::fx::tape::Character::Studio);
    hw.tog_sw18.position = daisy::Switch3::POS_DOWN; Tick(20);
    assert(tape_fx.Requested().drop_type == ::seed::fx::tape::Character::Cassette);
    // Wet output differs from the input at Mix 100 %.
    double difference = 0;
    for(unsigned b = 0; b < 200; ++b) {
        Tick();
        for(unsigned i = 0; i < kN; ++i) difference += std::fabs(out_l[i] - in_l[i]);
    }
    assert(difference > 10);
    // SW17 = bypass: within the 10 ms glide the output becomes exactly the input.
    hw.tog_sw17.pressed = true;
    Tick(12);
    Tick(100, true);
    RenderLeds(ui_state.load());
    assert(hw.led[7] == 0.f);
    // Mono source: without a right plug both channels carry the left input.
    hw.right_detect.pressed = false;
    Tick(50, true);
    hw.tog_sw17.pressed = false;
    hw.pot[6].value = 0; hw.pot[7].value = 0; // no hiss, no dropouts: L/R identical
    Tick(200);
    for(unsigned i = 0; i < kN; ++i) assert(out_l[i] == out_r[i]);
    // Overrun indicator from the cycle counter (fake: every callback measures `step` cycles).
    budget_cycles = 1000;
    DWT->CYCCNT.step = 900;
    Tick(5);
    assert(overruns.load() == 0 && maximum_cycles.load() == 900);
    RenderLeds(ui_state.load());
    assert(hw.led[15] == 0.f);
    DWT->CYCCNT.step = 2000;
    Tick(3);
    DWT->CYCCNT.step = 0;
    Tick(1);
    RenderLeds(ui_state.load());
    assert(overruns.load() == 3 && maximum_cycles.load() == 2000 && hw.led[15] == .5f);
    // Wrong block size: silence, no DSP state touched.
    {
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        out_l[0] = 1;
        AudioCallback(in, out, 32);
        assert(out_l[0] == 0);
    }
    // Bootloader S9+S16 held alone for 5 s.
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    Tick(5100);
    ServiceBootloader();
    assert(daisy::System::bootloader_resets == resets + 1);
    hw.pressed.fill(false); Tick(10);
    assert(allocations == 0);
    std::puts("PASS tape-fx firmware: pots, S1-S6 options with LEDs, SW18 character, SW17 exact bypass, mono input, "
              "overrun counter, block-size guard, bootloader keys, no allocation");
}
