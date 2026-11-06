// MIT. The production filter-fx firmware (src/main.cpp) on the Step Seq host fakes: pots,
// mode and LFO keys, SW17 with the 10 ms fade (exact dry when off), SW18, mono input, MIDI
// CC, SQ, the bootloader keys and no allocation in the callback.
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
    namespace ff = seed::fx::filter_fx;
    for(auto& p : hw.pot)
        p.value = .5f;
    hw.right_detect.pressed = true;
    hw.tog_sw17.pressed = true;
    hw.tog_sw18.position = daisy::Switch3::POS_UP;
    CHECK(Initialize());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(100);
    CHECK(params.enabled && std::fabs(params.cutoff - 79.5f) < .5f && std::fabs(params.resonance - 50.f) < .5f);
    CHECK(std::fabs(params.mix - 50.f) < .5f && std::fabs(params.drive - 50.f) < .5f);
    CHECK(std::fabs(params.lfo_rate - 1.f) < .05f && std::fabs(params.lfo_depth - 24.f) < .5f);
    CHECK(std::fabs(params.env_amount) < .5f && std::fabs(params.env_release - 141.4f) < 3.f);
    CHECK(params.bass_comp == ff::BassComp::Full);
    CHECK(!dry_exact);
    // Pot ends.
    hw.pot[0].value = 1.f;
    hw.pot[4].value = 0.f;
    hw.pot[7].value = 1.f;
    Tick(300);
    CHECK(std::fabs(params.cutoff - 135.f) < .01f && std::fabs(params.lfo_rate - .05f) < .001f);
    CHECK(std::fabs(params.env_release - 2000.f) < 1.f);
    // S1-S8 mode, S10-S13 LFO shape, S14 stereo. S9 and S16 change nothing alone.
    for(int m = 0; m < 8; ++m) {
        Tap(m);
        CHECK(params.mode == ff::Mode(m));
        CHECK(hw.led[std::size_t(m)] > 0.f);
    }
    const auto mode = params.mode;
    Tap(8);
    Tap(15);
    CHECK(params.mode == mode);
    for(int s = 0; s < 4; ++s) {
        Tap(9 + s);
        CHECK(params.lfo_shape == ff::LfoShape(s));
        CHECK(hw.led[std::size_t(9 + s)] > 0.f);
    }
    Tap(13);
    CHECK(params.lfo_stereo && hw.led[13] > 0.f);
    Tap(13);
    CHECK(!params.lfo_stereo);
    // SW18 sets the bass compensation when it moves.
    hw.tog_sw18.position = daisy::Switch3::POS_CENTER;
    Tick(40);
    CHECK(params.bass_comp == ff::BassComp::Half);
    hw.tog_sw18.position = daisy::Switch3::POS_DOWN;
    Tick(40);
    CHECK(params.bass_comp == ff::BassComp::Off);
    // SW17 off: after the 10 ms fade the output is exactly the dry input.
    hw.tog_sw17.pressed = false;
    Tick(20);
    CHECK(dry_exact && !params.enabled && hw.led[14] == 0.f);
    // Mono input: the left jack feeds both channels.
    hw.right_detect.pressed = false;
    Tick(5);
    CHECK(dry_exact);
    hw.right_detect.pressed = true;
    // MIDI CC 20 (enabled) over DIN, CC 74 (cutoff) and CC 24 (log rate) over USB.
    daisy::MidiEvent cc;
    cc.type = daisy::ControlChange;
    cc.data[0] = 20;
    cc.data[1] = 127;
    hw.midi.incoming.push_back(cc);
    cc.data[0] = 74;
    cc.data[1] = 0;
    usb_midi.incoming.push_back(cc);
    cc.data[0] = 24;
    cc.data[1] = 127;
    usb_midi.incoming.push_back(cc);
    Tick(30);
    CHECK(params.enabled && params.cutoff == 24.f && std::fabs(params.lfo_rate - 20.f) < .01f && !dry_exact);
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
    // SET_PARAM resonance; once the callback took it, applied equals accepted.
    w.Begin(sq::SetParam, 4);
    w.U7(0);
    w.U14(4);
    w.I32(40000);
    const std::size_t set_size = w.End();
    e.sysex_message_len = uint8_t(set_size - 2);
    for(std::size_t i = 1; i + 1 < set_size; ++i)
        e.sysex_data[i - 1] = buffer[i];
    usb_midi.incoming.push_back(e);
    Tick(3);
    CHECK(std::fabs(params.resonance - 40.f) < 0.01f);
    CHECK(fw::device.AcceptedRevision() == 1 && fw::device.view.applied_revision == 1);
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
    std::printf("PASS: filter-fx firmware: pots, mode and LFO keys, SW17 exact dry after the fade, SW18, mono input, "
                "MIDI CC, SQ, bootloader keys, no allocation\n");
    return 0;
}
