// MIT. The production sc-reverb-fx firmware (src/main.cpp) on the Step Seq host fakes: pots, SW17 feed,
// dry at mix 0, wet at mix 1, bootloader keys, no allocation in the callback.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

static bool in_audio = false;
static unsigned allocations = 0;
[[gnu::noinline]] void* operator new(std::size_t n) {
    if(in_audio)
        ++allocations;
    if(void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static float in_l[48], in_r[48], out_l[48], out_r[48];
static unsigned sample = 0;
static bool dry_exact = true;
static float peak_difference = 0.f;

static void Tick(unsigned count = 1, bool sound = true) {
    dry_exact = true;
    peak_difference = 0.f;
    for(unsigned b = 0; b < count; ++b) {
        for(unsigned i = 0; i < 48; ++i, ++sample) {
            in_l[i] = sound ? .3f * std::sin(6.2831853f * 220.f * float(sample) / 48000.f) : 0.f;
            in_r[i] = sound ? .2f * std::sin(6.2831853f * 330.f * float(sample) / 48000.f) : 0.f;
        }
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        ++daisy::System::now;
        ServiceMain();
        for(unsigned i = 0; i < 48; ++i) {
            CHECK(std::isfinite(out_l[i]) && std::fabs(out_l[i]) <= 1.f);
            const float right = hw.right_detect.pressed ? in_r[i] : in_l[i];
            dry_exact = dry_exact && out_l[i] == in_l[i] && out_r[i] == right;
            peak_difference = std::fmax(peak_difference, std::fabs(out_l[i] - in_l[i]));
        }
    }
}

int main() {
    for(auto& p : hw.pot)
        p.value = 0.f;
    hw.pot[0].value = 0.8f;
    hw.pot[1].value = 0.5f;
    hw.right_detect.pressed = true;
    hw.tog_sw17.pressed = true;
    CHECK(Initialize());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(300);
    CHECK(dry_exact); // VR3 at zero: exact dry
    CHECK(std::fabs(reverb.Requested().feedback - 0.8f) < 1e-3f);
    CHECK(std::fabs(reverb.Requested().lp_hz - 100.f * std::pow(200.f, 0.5f)) < 1.f);
    hw.pot[2].value = 1.f;
    Tick(1000);
    CHECK(!dry_exact && peak_difference > 0.01f);
    // SW17 off: the feed closes, the tail rings on.
    hw.tog_sw17.pressed = false;
    Tick(30, false);
    CHECK(!host.Enabled());
    float tail = 0.f;
    for(float x : out_l)
        tail = std::fmax(tail, std::fabs(x));
    CHECK(tail > 0.f);
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    Tick(5100);
    CHECK(daisy::System::bootloader_resets == resets + 1);
    CHECK(allocations == 0);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: sc-reverb-fx firmware: pots, exact dry, wet, SW17 tail, bootloader keys, no allocation\n");
    return 0;
}
