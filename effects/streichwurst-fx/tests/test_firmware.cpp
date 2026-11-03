// SPDX-License-Identifier: MIT
// The production standalone Streichwurst FX firmware (src/main.cpp) on the Step Seq host
// fakes: exact pass-through with ensemble off and Animate/Phaser/Reverb at 0, mono input,
// ensemble modes and gain word, pots acting only when moved, MIDI CC, SQ identity and
// parameters with monitor feedback, S16 clear and the S9+S16 chord, the bootloader keys,
// bounded output and no allocation in the callback.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" {
USBD_HandleTypeDef hUsbDeviceHS;
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
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
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if(!(cond)) {                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while(0)

static float in_l[48], in_r[48], out_l[48], out_r[48];
static unsigned sample = 0;
static bool exact = true, bounded = true;
static double energy_l = 0, diff_lr = 0;

static void Tick(unsigned count = 1, bool silent = false) {
    for(unsigned b = 0; b < count; ++b) {
        for(unsigned i = 0; i < 48; ++i, ++sample) {
            in_l[i] = silent ? 0.f : .2f * std::sin(6.2831853f * 220.f * float(sample) / 48000.f);
            in_r[i] = silent ? 0.f : .15f * std::sin(6.2831853f * 330.f * float(sample) / 48000.f);
        }
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        ++daisy::System::now;
        ServiceMain();
        exact = true;
        for(unsigned i = 0; i < 48; ++i) {
            bounded = bounded && std::isfinite(out_l[i]) && std::fabs(out_l[i]) < 1.f && std::fabs(out_r[i]) < 1.f;
            const float right = hw.right_detect.pressed ? in_r[i] : in_l[i];
            const float feed = 0.5f * (in_l[i] + right) * 2.f; // input level 2 (VR7 at maximum)
            exact = exact && out_l[i] == feed && out_r[i] == feed;
            energy_l += double(out_l[i]) * out_l[i];
            diff_lr += std::fabs(out_l[i] - out_r[i]);
        }
    }
}

static daisy::MidiEvent SysEx(const std::vector<uint8_t>& frame) {
    daisy::MidiEvent e;
    e.type = daisy::SystemCommon;
    e.sc_type = daisy::SystemExclusive;
    e.sysex_message_len = uint8_t(frame.size() - 2);
    for(std::size_t i = 1; i + 1 < frame.size(); ++i)
        e.sysex_data[i - 1] = frame[i];
    return e;
}
static std::vector<uint8_t> Frame(uint8_t type, uint8_t rid, const std::vector<uint8_t>& payload) {
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(type, rid);
    for(uint8_t b : payload)
        w.U7(b);
    return std::vector<uint8_t>(buffer, buffer + w.End());
}
static void SetParam(uint8_t rid, uint16_t id, int32_t value) {
    std::vector<uint8_t> p = {0, uint8_t(id & 0x7F), uint8_t(id >> 7)};
    for(int i = 0; i < 5; ++i)
        p.push_back(uint8_t((uint32_t(value) >> (7 * i)) & 0x7F));
    usb_midi.incoming.push_back(SysEx(Frame(sq::SetParam, rid, p)));
}
static std::vector<uint8_t> Reply(uint8_t type, int rid) {
    for(auto it = usb_midi.sent.rbegin(); it != usb_midi.sent.rend(); ++it)
        if(it->size() > 8 && (*it)[2] == 0x53 && (*it)[5] == type && (*it)[6] == rid)
            return *it;
    return {};
}
static bool Contains(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}
static daisy::MidiEvent Cc(uint8_t cc, uint8_t value, uint8_t channel) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange;
    e.channel = channel - 1;
    e.data[0] = cc;
    e.data[1] = value;
    return e;
}

int main() {
    for(auto& p : hw.pot)
        p.value = 0.5f;
    hw.pot[6].value = 1.f; // input level 2
    hw.pot[7].value = 1.f; // output level 1
    CHECK(Initialize());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(5);
    // Power-up: contract defaults (pots have not moved), ensemble mode 1 on, reverb 20 %.
    CHECK(state.fx.ensemble_on && state.fx.ensemble_mode == 1 && state.fx.reverb_wet == 0.2f);

    // SQ: identity with the one FX component.
    usb_midi.incoming.push_back(SysEx(Frame(sq::Identify, 1, {})));
    Tick();
    std::vector<uint8_t> r = Reply(sq::Identify | sq::kResponse, 1);
    CHECK(Contains(r, "streichwurst-fx") && Contains(r, STREICHWURST_FX_FIRMWARE_VERSION));
    CHECK(r[r.size() - 4] == 1);
    usb_midi.incoming.push_back(SysEx(Frame(sq::GetComponent, 2, {0})));
    Tick();
    r = Reply(sq::GetComponent | sq::kResponse, 2);
    CHECK(Contains(r, "streichwurst-fx") && Contains(r, STREICHWURST_FX_VERSION));

    // Ensemble off, reverb 0 over SQ: the string stage passes the input exactly.
    SetParam(3, streichwurst::product::id::EnsembleOn, 0);
    SetParam(4, streichwurst::product::id::Reverb, 0);
    Tick(3);
    CHECK(!Reply(sq::SetParam | sq::kResponse, 3).empty() && !Reply(sq::SetParam | sq::kResponse, 4).empty());
    CHECK(fw::device.view.applied_revision == 2);
    Tick(1200); // the 20 ms ensemble fade and the 20 ms wet smoothing have settled
    bool all_exact = true;
    for(int i = 0; i < 50; ++i) {
        Tick();
        all_exact = all_exact && exact;
    }
    CHECK(all_exact);
    // Mono source in the left jack feeds both sides.
    hw.right_detect.pressed = false;
    Tick(5);
    CHECK(exact);
    hw.right_detect.pressed = true;

    // S2 selects mode 1, S12 switches the ensemble on: stereo output and the gain word.
    hw.pressed[11] = true; // keys are debounced over several 1 ms ticks
    Tick(20);
    hw.pressed[11] = false;
    Tick(20);
    hw.pressed[1] = true;
    Tick(20);
    hw.pressed[1] = false;
    Tick(100);
    CHECK(state.fx.ensemble_on && state.fx.ensemble_mode == 1);
    diff_lr = 0;
    Tick(200);
    CHECK(diff_lr > 1.0); // the stereo tap correction separates the arms
    hw.pressed[2] = true; // S3: mode 2
    Tick(20);
    hw.pressed[2] = false;
    Tick(20);
    CHECK(state.fx.ensemble_mode == 2);

    // A pot acts when it moves: VR5 (reverb) to 80 %; the monitor reports it.
    usb_midi.incoming.push_back(SysEx(Frame(sq::Monitor, 5, {1})));
    Tick(3);
    hw.pot[4].value = 0.8f;
    Tick(300);
    CHECK(std::fabs(state.fx.reverb_wet - 0.8f) < 0.01f);
    bool reported = false;
    for(auto& m : usb_midi.sent)
        reported = reported || (m.size() > 12 && m[2] == 0x53 && m[5] == sq::kMonitorEvent && m[8] == sq::EvParam &&
                                m[10] == streichwurst::product::id::Reverb);
    CHECK(reported);
    // MIDI CC (any channel, DIN): CC106 phaser 100 %; instrument CCs are ignored here.
    hw.midi.incoming.push_back(Cc(106, 127, 9));
    hw.midi.incoming.push_back(Cc(20, 0, 1)); // instrument registration: not part of this FX
    Tick(3);
    CHECK(state.fx.phaser_amount == 1.f);

    // S16 clears line and reverb on release: the tail stops at once.
    Tick(200);
    hw.pressed[15] = true;
    Tick(20, true);
    energy_l = 0;
    Tick(20, true);
    const double before_clear = energy_l; // the reverb tail still rings while S16 is held
    hw.pressed[15] = false;
    Tick(30, true);
    energy_l = 0;
    Tick(20, true);
    std::printf("  S16: tail energy %.3g before, %.3g after the clear\n", before_clear, energy_l);
    CHECK(before_clear > 1e-6);
    CHECK(energy_l < before_clear * 1e-6);
    // S9+S16 together: no clear, the bootloader after 5 s of holding.
    Tick(200);
    hw.pressed[8] = hw.pressed[15] = true;
    for(int i = 0; i < 5100 && !boot_request.Pending(); ++i)
        Tick(1, true);
    CHECK(daisy::System::bootloader_resets == 1); // S9+S16 held 5 s, no S16 clear meanwhile
    hw.pressed[8] = hw.pressed[15] = false;
    Tick(3, true);

    CHECK(bounded);
    CHECK(allocations == 0);
    if(failures) {
        std::printf("streichwurst-fx firmware: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS: standalone Streichwurst FX firmware on host fakes (exact pass-through, mono input, modes, "
                "pots, CC, SQ with monitor, S16 clear, bootloader keys, no audio allocation)\n");
    return 0;
}
