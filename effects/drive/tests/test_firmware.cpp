// MIT. The production drive-fx firmware (src/main.cpp) on the Step Seq host fakes: pots,
// curve and auto-level keys, SW17 with the 10 ms fade (exact dry when off), mono input,
// MIDI CC, SQ, the bootloader keys and no allocation in the callback.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

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

static void Tick(unsigned count = 1) {
    for(unsigned b = 0; b < count; ++b) {
        for(unsigned i = 0; i < 48; ++i, ++sample) {
            in_l[i] = .4f * std::sin(6.2831853f * 220.f * float(sample) / 48000.f);
            in_r[i] = .3f * std::sin(6.2831853f * 330.f * float(sample) / 48000.f);
        }
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        ++daisy::System::now;
        ServiceMain();
        dry_exact = true;
        for(unsigned i = 0; i < 48; ++i) {
            CHECK(std::isfinite(out_l[i]) && std::fabs(out_l[i]) <= 1.f);
            const float right = hw.right_detect.pressed ? in_r[i] : in_l[i];
            dry_exact &= out_l[i] == in_l[i] && out_r[i] == right;
        }
    }
}

static void Tap(int key) {
    hw.pressed[std::size_t(key)] = true;
    Tick(20);
    hw.pressed[std::size_t(key)] = false;
    Tick(20);
}

int main() {
    for(auto& p : hw.pot)
        p.value = .5f;
    hw.right_detect.pressed = true;
    hw.tog_sw17.pressed = true;
    CHECK(Initialize());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(100);
    CHECK(params.enabled && std::fabs(params.drive - 50.f) < .5f && std::fabs(params.level_db + 6.f) < .5f);
    CHECK(!dry_exact); // the drive is on
    // S1-S4 curve, S5 auto level.
    Tap(1);
    CHECK(params.curve == seed::fx::drive::Curve::Hard);
    Tap(3);
    CHECK(params.curve == seed::fx::drive::Curve::Fold);
    Tap(4);
    CHECK(!params.auto_level);
    Tap(4);
    CHECK(params.auto_level);
    // SW17 off: after the 10 ms fade the output is exactly the dry input.
    hw.tog_sw17.pressed = false;
    Tick(20);
    CHECK(dry_exact && !params.enabled);
    // Mono input: the left jack feeds both channels.
    hw.right_detect.pressed = false;
    Tick(5);
    CHECK(dry_exact);
    hw.right_detect.pressed = true;
    // MIDI CC 20 (enabled) over DIN, CC 22 (drive) over USB.
    daisy::MidiEvent cc;
    cc.type = daisy::ControlChange;
    cc.data[0] = 20;
    cc.data[1] = 127;
    hw.midi.incoming.push_back(cc);
    cc.data[0] = 22;
    cc.data[1] = 0;
    usb_midi.incoming.push_back(cc);
    Tick(30);
    CHECK(params.enabled && params.drive == 0.f && !dry_exact);
    // SQ IDENTIFY over USB.
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(sq::Identify, 3);
    const std::size_t size = w.End();
    daisy::MidiEvent e;
    e.type = daisy::SystemCommon;
    e.sc_type = daisy::SystemExclusive;
    e.sysex_message_len = uint8_t(size - 2);
    for(std::size_t i = 1; i + 1 < size; ++i)
        e.sysex_data[i - 1] = buffer[i];
    usb_midi.incoming.push_back(e);
    Tick(2);
    CHECK(!usb_midi.sent.empty() && usb_midi.sent.back()[5] == (sq::Identify | sq::kResponse));
    // Bootloader keys.
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    Tick(5100);
    CHECK(daisy::System::bootloader_resets == resets + 1);
    CHECK(allocations == 0);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: drive-fx firmware: pots, curve/auto-level keys, SW17 exact dry after the fade, mono input, "
                "MIDI CC, SQ, bootloader keys, no allocation\n");
    return 0;
}
