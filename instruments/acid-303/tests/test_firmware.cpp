// MIT. The production Acid 303 firmware (src/main.cpp) on the Step Seq host fakes: keys
// play notes, pots write only when they move, MIDI notes and CCs (USB and DIN), SQ
// parameters, USB loss, the bootloader after muting, and no allocation in the callback.
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

static float out_l[48], out_r[48];
static float peak = 0.f;

static void Tick(unsigned count = 1) {
    for(unsigned b = 0; b < count; ++b) {
        const float* in[2] = {out_l, out_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        ++daisy::System::now;
        ServiceMain();
        for(unsigned i = 0; i < 48; ++i) {
            CHECK(std::isfinite(out_l[i]) && out_l[i] == out_r[i]);
            peak = std::fmax(peak, std::fabs(out_l[i]));
        }
    }
}

static daisy::MidiEvent Note(bool on, uint8_t note, uint8_t velocity = 100) {
    daisy::MidiEvent e;
    e.type = on ? daisy::NoteOn : daisy::NoteOff;
    e.data[0] = note;
    e.data[1] = velocity;
    return e;
}
static daisy::MidiEvent Cc(uint8_t number, uint8_t value) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange;
    e.data[0] = number;
    e.data[1] = value;
    return e;
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

int main() {
    for(auto& p : hw.pot)
        p.value = 0.5f;
    Initialize();
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(20);
    CHECK(peak == 0.f && !voice.Gate());
    // A key plays C2 + index; release ends the note.
    hw.pressed[4] = true;
    Tick(100);
    CHECK(voice.Gate() && voice.SoundingNote() == 40 && peak > 0.01f);
    hw.pressed[4] = false;
    Tick(100);
    CHECK(!voice.Gate());
    // Pots act at start-up and when they move; a CC wins until the pot moves again.
    CHECK(std::fabs(params.resonance - 50.f) < 0.5f);
    hw.midi.incoming.push_back(Cc(71, 127)); // resonance over DIN
    Tick(5);
    CHECK(params.resonance == 100.f);
    Tick(50);
    CHECK(params.resonance == 100.f);
    hw.pot[1].value = 0.2f;
    Tick(200);
    CHECK(std::fabs(params.resonance - 20.f) < 0.5f);
    // MIDI notes over USB (omni); velocity above 100 is an accent; CC 123 ends them.
    usb_midi.incoming.push_back(Note(true, 60, 127));
    Tick(5);
    CHECK(voice.Gate() && voice.SoundingNote() == 60);
    daisy::MidiEvent off;
    off.type = daisy::ChannelMode;
    off.cm_type = daisy::AllNotesOff;
    usb_midi.incoming.push_back(off);
    Tick(5);
    CHECK(!voice.Gate());
    // SQ: IDENTIFY names the product; SET_PARAM cutoff.
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(sq::Identify, 1);
    usb_midi.incoming.push_back(SysEx(std::vector<uint8_t>(buffer, buffer + w.End())));
    Tick(2);
    CHECK(!usb_midi.sent.empty() && usb_midi.sent.back()[5] == (sq::Identify | sq::kResponse));
    w.Begin(sq::SetParam, 2);
    w.U7(0);
    w.U14(4);
    w.I32(2000000);
    usb_midi.incoming.push_back(SysEx(std::vector<uint8_t>(buffer, buffer + w.End())));
    Tick(3);
    CHECK(params.cutoff == 2000.f);
    // F10: the applied revision follows the accepted one once the callback took it.
    CHECK(fw::device.AcceptedRevision() == 1 && fw::device.view.applied_revision == 1);
    // F12: an SQ slide mode stays until SW18 moves; then the switch wins.
    w.Begin(sq::SetParam, 3);
    w.U7(0);
    w.U14(11);
    w.I32(2000);
    usb_midi.incoming.push_back(SysEx(std::vector<uint8_t>(buffer, buffer + w.End())));
    Tick(50);
    CHECK(params.slide_mode == acid303::SlideMode::Always && fw::device.view.applied_revision == 2);
    hw.tog_sw18.position = daisy::Switch3::POS_CENTER;
    Tick(50);
    CHECK(params.slide_mode == acid303::SlideMode::Legato);
    hw.tog_sw18.position = daisy::Switch3::POS_UP;
    Tick(50);
    CHECK(params.slide_mode == acid303::SlideMode::Off);
    // F01: a full command queue drops CCs but never a Note-Off, All-Off or Mute.
    usb_midi.incoming.push_back(Note(true, 64));
    Tick(3);
    CHECK(voice.Gate() && voice.SoundingNote() == 64);
    for(int i = 0; i < 80; ++i)
        usb_midi.incoming.push_back(Cc(74, uint8_t(i)));
    usb_midi.incoming.push_back(Note(false, 64));
    Tick(3);
    CHECK(!voice.Gate() && fw::device.counters[sq::CntMidiQueueOverflow] > 0);
    usb_midi.incoming.push_back(Note(true, 65));
    Tick(3);
    for(int i = 0; i < 80; ++i)
        usb_midi.incoming.push_back(Cc(74, uint8_t(i)));
    daisy::MidiEvent all_off;
    all_off.type = daisy::ChannelMode;
    all_off.cm_type = daisy::AllNotesOff;
    usb_midi.incoming.push_back(all_off);
    Tick(3);
    CHECK(!voice.Gate());
    // USB lost: MIDI notes end.
    usb_midi.incoming.push_back(Note(true, 62));
    Tick(3);
    CHECK(voice.Gate());
    hUsbDeviceHS.dev_state = 0;
    Tick(3);
    CHECK(!voice.Gate());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    // Peak over a loud phrase with maximum resonance stays below 0 dBFS.
    hw.pot[1].value = 1.f;
    hw.pot[6].value = 1.f;
    hw.pot[4].value = 1.f;
    hw.tog_sw17.pressed = true;
    for(int k = 0; k < 16; ++k) {
        hw.pressed[k] = true;
        Tick(60);
        hw.pressed[k] = false;
        Tick(40);
    }
    CHECK(peak < 1.f);
    // Bootloader: S9 + S16 for 5 s -> muted voice -> reset.
    const unsigned resets = daisy::System::bootloader_resets;
    hw.pressed[8] = hw.pressed[15] = true;
    for(int i = 0; i < 5200 && daisy::System::bootloader_resets == resets; ++i)
        Tick();
    CHECK(daisy::System::bootloader_resets == resets + 1 && !voice.Gate());
    CHECK(allocations == 0);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: production callback and main loop: keys, pots vs CC, MIDI USB/DIN, SQ, USB loss, "
                "peak < 0 dBFS, bootloader after mute, no audio allocation\n");
    return 0;
}
