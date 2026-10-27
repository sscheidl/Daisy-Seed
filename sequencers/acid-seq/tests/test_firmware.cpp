// MIT. The production Acid Seq firmware (src/main.cpp) on the Step Seq host fakes: power-up
// on Run with audio, CV locked and MIDI out, IDENTIFY with four components, SQ parameters
// of every component, notes and CCs on the voice channel (USB and DIN), voice ownership
// with the sequencer (R16), tempo hand-over to the delay, USB loss, the bootloader after
// muting, and no allocation in the callback.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
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
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static float out_l[48], out_r[48];
static float peak = 0.f;
static unsigned dac_writes = 0;

static void Tick(unsigned count = 1) {
    for(unsigned b = 0; b < count; ++b) {
        const float* in[2] = {out_l, out_r};
        float* out[2] = {out_l, out_r};
        const uint32_t trig = fakeDac.SWTRIGR;
        fakeDac.SWTRIGR = 0;
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        if(fakeDac.SWTRIGR == 3)
            ++dac_writes;
        else
            fakeDac.SWTRIGR = trig;
        for(unsigned i = 0; i < 48; ++i)
            peak = std::max(peak, std::max(std::fabs(out_l[i]), std::fabs(out_r[i])));
        ++daisy::System::now;
        ServiceMain();
    }
}

// Messages with exactly this status byte (Note-On on channel 1 is 0x90).
static int Count(const std::vector<std::vector<uint8_t>>& sent, uint8_t status) {
    int n = 0;
    for(auto& m : sent)
        n += m.size() == 3 && m[0] == status;
    return n;
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

static daisy::MidiEvent SetParam(uint8_t rid, uint8_t component, uint16_t id, int32_t value) {
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(sq::SetParam, rid);
    w.U7(component);
    w.U14(id);
    w.I32(value);
    return SysEx(std::vector<uint8_t>(buffer, buffer + w.End()));
}

static daisy::MidiEvent Note(bool on, uint8_t note, uint8_t channel, uint8_t velocity = 100) {
    daisy::MidiEvent e;
    e.type = on ? daisy::NoteOn : daisy::NoteOff;
    e.channel = channel - 1;
    e.data[0] = note;
    e.data[1] = velocity;
    return e;
}

static daisy::MidiEvent Cc(uint8_t cc, uint8_t value, uint8_t channel) {
    daisy::MidiEvent e;
    e.type = daisy::ControlChange;
    e.channel = channel - 1;
    e.data[0] = cc;
    e.data[1] = value;
    return e;
}

static std::vector<uint8_t> LastSysEx() {
    for(auto it = usb_midi.sent.rbegin(); it != usb_midi.sent.rend(); ++it)
        if(it->size() > 5 && (*it)[0] == 0xF0)
            return *it;
    return std::vector<uint8_t>(16, 0);
}

static bool Contains(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

static const acid303::Voice& V() { return chain.GetVoice(); }

int main() {
    // Power-up with SW17 on Run: the sequencer starts and plays the voice.
    hw.tog_sw17.pressed = true;
    for(auto& p : hw.pot)
        p.value = 0.5f;
    Initialize();
    CHECK(hw.dac_init.size() == 1 && hw.dac_init[0].second == fw::kStartupDacCode);
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(2000);
    CHECK(engine.GetState() == step_seq::State::Running);
    CHECK(peak > 0.05f && peak <= 1.f);
    CHECK(Count(usb_midi.sent, 0x90) >= 5 && Count(hw.midi.sent, 0x90) == Count(usb_midi.sent, 0x90));
    CHECK(dac_writes == 0); // CV stays locked until enabled (4.5)
    // Tempo hand-over: the delay follows the sequencer (VR5 at 0.5).
    CHECK(std::fabs(chain.GetDelay().Tempo() - 48000.f * 60.f / engine.CurrentBpm()) < 0.5f);

    // IDENTIFY: product, version and the four components.
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(sq::Identify, 1);
    usb_midi.incoming.push_back(SysEx(std::vector<uint8_t>(buffer, buffer + w.End())));
    Tick();
    std::vector<uint8_t> reply = LastSysEx();
    CHECK(reply[5] == (sq::Identify | sq::kResponse));
    CHECK(Contains(reply, "acid-seq") && Contains(reply, ACID_SEQ_VERSION));
    CHECK(reply[reply.size() - 4] == fw::kComponents); // component count before the CRC
    const char* names[] = {"step-seq", "acid-303", "drive", "stereo-delay"};
    for(uint8_t k = 0; k < fw::kComponents; ++k) {
        w.Begin(sq::GetComponent, uint8_t(10 + k));
        w.U7(k);
        usb_midi.incoming.push_back(SysEx(std::vector<uint8_t>(buffer, buffer + w.End())));
        Tick();
        reply = LastSysEx();
        CHECK(reply[5] == (sq::GetComponent | sq::kResponse) && Contains(reply, names[k]));
    }

    // SQ parameters of the voice, Drive and Delay reach the chain.
    usb_midi.incoming.push_back(SetParam(2, fw::kVoice, 4, 2000000)); // cutoff 2 kHz
    usb_midi.incoming.push_back(SetParam(3, fw::kDrive, 3, 80000));   // drive 80 %
    usb_midi.incoming.push_back(SetParam(4, fw::kDelay, 7, 50000));   // dry/wet 50 %
    Tick(3);
    CHECK(chain.voice_params.cutoff == 2000.f && chain.drive_params.drive == 80.f && chain.delay_params.mix == 50.f);
    CHECK(fw::voice_values[3] == 2000000 && fw::drive_values[2] == 80000 && fw::delay_values[6] == 50000);

    // CCs on the voice channel (2) over DIN set the voice and the effects; other channels not.
    hw.midi.incoming.push_back(Cc(74, 0, 2));   // cutoff -> 40 Hz
    hw.midi.incoming.push_back(Cc(107, 0, 2));  // delay feedback -> 0
    hw.midi.incoming.push_back(Cc(71, 127, 1)); // resonance on the sequencer channel: ignored
    Tick(3);
    CHECK(chain.voice_params.cutoff == 40.f && chain.delay_params.feedback == 0.f &&
          chain.voice_params.resonance == 50.f);

    // Stop the sequencer: a MIDI note on the voice channel plays the voice, Note-Off ends it.
    hw.tog_sw17.pressed = false;
    Tick(100);
    CHECK(engine.GetState() == step_seq::State::Stop && !V().Gate());
    hw.midi.incoming.push_back(Note(true, 60, 2));
    Tick(3);
    CHECK(V().Gate() && V().SoundingNote() == 60);
    usb_midi.incoming.push_back(Note(true, 64, 1)); // wrong channel: ignored
    Tick(3);
    CHECK(V().SoundingNote() == 60);
    // The MIDI note is not forwarded to the MIDI outputs.
    CHECK(std::none_of(hw.midi.sent.begin(), hw.midi.sent.end(),
                       [](const std::vector<uint8_t>& m) { return m.size() == 3 && m[0] == 0x91 && m[1] == 60; }));
    // Ownership (R16): the sequencer starts and wins; stopping returns the held MIDI note.
    hw.tog_sw17.pressed = true;
    Tick(400);
    CHECK(engine.GetState() == step_seq::State::Running && V().Gate() && V().SoundingNote() != 60);
    hw.tog_sw17.pressed = false;
    Tick(100);
    CHECK(V().Gate() && V().SoundingNote() == 60);
    // A sequencer panic (seq channel change) leaves the MIDI note alone.
    usb_midi.incoming.push_back(SetParam(5, fw::kSeq, 12, 3000));
    Tick(5);
    CHECK(engine.settings.seq_channel == 3 && V().Gate() && V().SoundingNote() == 60);
    hw.midi.incoming.push_back(Note(false, 60, 2));
    Tick(3);
    CHECK(!V().Gate());
    // A new voice channel ends notes held on the old one.
    usb_midi.incoming.push_back(Note(true, 62, 2));
    Tick(3);
    CHECK(V().Gate());
    usb_midi.incoming.push_back(SetParam(6, fw::kSeq, 13, 5000));
    Tick(5);
    CHECK(engine.settings.voice_channel == 5 && !V().Gate());
    // USB lost: MIDI notes end; reconnect.
    usb_midi.incoming.push_back(Note(true, 65, 5));
    Tick(3);
    CHECK(V().Gate() && V().SoundingNote() == 65);
    hUsbDeviceHS.dev_state = 0;
    Tick(3);
    CHECK(!V().Gate());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(3);

    // Delay off over SQ reaches the host side of the chain (tail and exact dry: test_acid_seq).
    usb_midi.incoming.push_back(SetParam(7, fw::kDelay, 1, 0));
    Tick(3);
    CHECK(!chain.delay_params.enabled && chain.GetDelayHost().GetState() != ::seed::fx::DelayHost::State::On);

    // Bootloader: S9 + S16 for 5 s -> panic and mute -> reset.
    hw.tog_sw17.pressed = true;
    Tick(500);
    CHECK(engine.GetState() == step_seq::State::Running);
    hw.pressed[8] = hw.pressed[15] = true;
    unsigned resets = daisy::System::bootloader_resets;
    for(int i = 0; i < 5200 && daisy::System::bootloader_resets == resets; ++i)
        Tick();
    CHECK(daisy::System::bootloader_resets == resets + 1);
    CHECK(!engine.GateHigh() && !V().Gate());
    CHECK(!fw::usb_port.AnySent(3) && !fw::din_port.AnySent(3));
    hw.pressed[8] = hw.pressed[15] = false;
    Tick(20);
    // SysEx bootloader request: acknowledged, then reset.
    std::vector<uint8_t> boot(std::begin(bootloader::kRequest), std::end(bootloader::kRequest));
    usb_midi.incoming.push_back(SysEx(boot));
    resets = daisy::System::bootloader_resets;
    Tick();
    CHECK(daisy::System::bootloader_resets == resets + 1);

    CHECK(allocations == 0);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: production callback and main loop: power-up on Run with audio, CV locked, MIDI out, "
                "IDENTIFY with four components, SQ parameters per component, voice-channel notes and CCs over "
                "USB/DIN, voice ownership with the sequencer, tempo hand-over, USB loss, bootloader after mute, "
                "no audio allocation\n");
    return 0;
}
