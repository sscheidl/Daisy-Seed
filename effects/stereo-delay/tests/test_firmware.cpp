// MIT. The production delay-fx firmware (src/main.cpp) on the Step Seq host fakes: pots,
// SW18 mode/sync, SW17 off with tail and exact dry afterwards, tempo from VR5 and from
// MIDI clock (and back after it stops), MIDI CC, SQ, bootloader keys, no allocation.
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

static void Tick(unsigned count = 1, bool sound = true) {
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
        dry_exact = true;
        for(unsigned i = 0; i < 48; ++i) {
            CHECK(std::isfinite(out_l[i]) && std::fabs(out_l[i]) <= 1.f);
            const float right = hw.right_detect.pressed ? in_r[i] : in_l[i];
            dry_exact &= std::fabs(out_l[i] - in_l[i]) < 1e-7f && std::fabs(out_r[i] - right) < 1e-7f;
        }
    }
}

int main() {
    for(auto& p : hw.pot)
        p.value = .5f;
    hw.right_detect.pressed = true;
    hw.tog_sw17.pressed = true;
    hw.tog_sw18.position = daisy::Switch3::POS_CENTER; // Ping-Pong synced
    CHECK(Initialize());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(1000);
    CHECK(params.enabled && params.sync && params.mode == ::seed::fx::delay::Mode::PingPong);
    CHECK(std::fabs(delay.Tempo() - 48000.f * 60.f / 165.f) < 1.f); // VR5 at half: 165 BPM
    CHECK(!dry_exact);
    // SW18 down: Stereo L/R synced; up: Ping-Pong free.
    hw.tog_sw18.position = daisy::Switch3::POS_DOWN;
    Tick(30);
    CHECK(params.mode == ::seed::fx::delay::Mode::Stereo && params.sync);
    hw.tog_sw18.position = daisy::Switch3::POS_UP;
    Tick(30);
    CHECK(params.mode == ::seed::fx::delay::Mode::PingPong && !params.sync);
    // MIDI clock at 120 BPM over DIN sets the tempo; after it stops, VR5 again.
    daisy::MidiEvent clock;
    clock.type = daisy::SystemRealTime;
    clock.srt_type = daisy::TimingClock;
    for(int i = 0; i < 60; ++i) {
        hw.midi.incoming.push_back(clock);
        Tick(i % 2 ? 21 : 21); // 21 ms per clock = 119 BPM
    }
    CHECK(external_tempo && std::fabs(delay.Tempo() - 21.f * 24.f * 48.f) < 50.f);
    Tick(2100);
    CHECK(!external_tempo);
    // SW17 off: the tail rings, then the output is exactly dry (unity bypass, Codex Q1/F15).
    hw.tog_sw17.pressed = false;
    Tick(50);
    CHECK(!dry_exact && host.GetState() == ::seed::fx::DelayHost::State::Tail);
    for(int i = 0; i < 25000 && host.GetState() != ::seed::fx::DelayHost::State::Off; ++i)
        Tick(1, false);
    CHECK(host.GetState() == ::seed::fx::DelayHost::State::Off);
    Tick(10);
    CHECK(dry_exact);
    // CC 107 (feedback) and SQ IDENTIFY.
    daisy::MidiEvent cc;
    cc.type = daisy::ControlChange;
    cc.data[0] = 107;
    cc.data[1] = 127;
    usb_midi.incoming.push_back(cc);
    Tick(3);
    CHECK(params.feedback == 95.f);
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(sq::Identify, 9);
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
    // F10: SET_PARAM feedback; once the callback took it, applied equals accepted.
    w.Begin(sq::SetParam, 10);
    w.U7(0);
    w.U14(6);
    w.I32(40000);
    const std::size_t set_size = w.End();
    e.sysex_message_len = uint8_t(set_size - 2);
    for(std::size_t i = 1; i + 1 < set_size; ++i)
        e.sysex_data[i - 1] = buffer[i];
    usb_midi.incoming.push_back(e);
    Tick(3);
    CHECK(params.feedback == 40.f);
    CHECK(fw::device.AcceptedRevision() == 1 && fw::device.view.applied_revision == 1);
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    Tick(5100);
    CHECK(daisy::System::bootloader_resets == resets + 1);
    CHECK(allocations == 0);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: delay-fx firmware: pots, SW18 mode/sync, VR5 and MIDI-clock tempo, SW17 tail then exact dry, "
                "MIDI CC, SQ, bootloader keys, no allocation\n");
    return 0;
}
