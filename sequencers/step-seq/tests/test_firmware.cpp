// MIT. The production Step Seq firmware (src/main.cpp) on host fakes: power-up with SW17
// on Run, the locked/enabled DAC path, SQ over USB, MIDI clock over DIN, USB loss and
// reconnect, the bootloader gesture after a panic, and no allocation in the callback.
#define main FirmwareMain
#include "../src/main.cpp"
#undef main
extern "C" { USBD_HandleTypeDef hUsbDeviceHS; }
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
        ++daisy::System::now;
        ServiceMain();
        for(unsigned i = 0; i < 48; ++i)
            CHECK(out_l[i] == 0.f && out_r[i] == 0.f);
    }
}

static int Count(const std::vector<std::vector<uint8_t>>& sent, uint8_t status, std::size_t size = 3) {
    int n = 0;
    for(auto& m : sent)
        n += m.size() == size && (m[0] & (size == 1 ? 0xFF : 0xF0)) == status;
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

static std::vector<uint8_t> Frame(uint8_t type, uint8_t rid, const std::vector<uint8_t>& payload) {
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(type, rid);
    for(uint8_t b : payload)
        w.U7(b);
    return std::vector<uint8_t>(buffer, buffer + w.End());
}

static void U32(std::vector<uint8_t>& v, uint32_t x) {
    for(int k = 0; k < 5; ++k)
        v.push_back(uint8_t((x >> (7 * k)) & 0x7F));
}

static std::vector<uint8_t> LastSysEx() {
    for(auto it = usb_midi.sent.rbegin(); it != usb_midi.sent.rend(); ++it)
        if(it->size() > 5 && (*it)[0] == 0xF0)
            return *it;
    return std::vector<uint8_t>(16, 0);
}

int main() {
    // Power-up with SW17 on Run: the sequencer starts after initialization.
    hw.tog_sw17.pressed = true;
    for(auto& p : hw.pot)
        p.value = 0.5f;
    Initialize();
    CHECK(hw.dac_init.size() == 1 && hw.dac_init[0].first == int(DacHandle::Channel::BOTH) &&
          hw.dac_init[0].second == fw::kStartupDacCode);
    CHECK(usb_midi.periph == MidiUsbTransport::Config::EXTERNAL && usb_midi.receiving && hw.midi.receiving);
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(1000);
    CHECK(engine.GetState() == step_seq::State::Running);
    CHECK(Count(usb_midi.sent, 0x90) >= 5 && Count(hw.midi.sent, 0x90) == Count(usb_midi.sent, 0x90));
    CHECK(Count(usb_midi.sent, 0xF8, 1) >= 30);
    // Locked CV: the callback never touched the DAC registers.
    CHECK(dac_writes == 0 && fakeDac.DHR12R1 == 0 && fakeDac.DHR12R2 == 0);

    // SQ over USB: IDENTIFY with the UID, then calibration and enable: the callback writes.
    usb_midi.incoming.push_back(SysEx(Frame(sq::Identify, 1, {})));
    Tick();
    std::vector<uint8_t> reply = LastSysEx();
    CHECK(reply[5] == (sq::Identify | sq::kResponse));
    sq::Reader r(reply.data() + 8, reply.size() - 11);
    r.U7();
    r.U7();
    char text[20];
    r.Str(text, sizeof(text));
    CHECK(std::string(text) == "step-seq");
    r.Str(text, sizeof(text));
    CHECK(std::string(text) == STEP_SEQ_FIRMWARE_VERSION);
    CHECK(r.U32() == (0x1234ABCDu ^ HAL_GetUIDw0()));
    CHECK(r.U32() == HAL_GetUIDw0() && r.U32() == HAL_GetUIDw1() && r.U32() == HAL_GetUIDw2());
    for(uint8_t output = 0; output < 2; ++output) {
        std::vector<uint8_t> cal;
        U32(cal, HAL_GetUIDw0());
        U32(cal, HAL_GetUIDw1());
        U32(cal, HAL_GetUIDw2());
        cal.push_back(output);
        U32(cal, 2048000);
        U32(cal, uint32_t(-409600));
        for(uint16_t code : {uint16_t(2048), uint16_t(410), uint16_t(0), uint16_t(4095)}) {
            cal.push_back(code & 0x7F);
            cal.push_back(uint8_t(code >> 7));
        }
        usb_midi.incoming.push_back(SysEx(Frame(sq::SetCal, uint8_t(2 + output), cal)));
        Tick();
        CHECK(LastSysEx()[5] == (sq::SetCal | sq::kResponse));
    }
    std::vector<uint8_t> enable;
    U32(enable, HAL_GetUIDw0());
    U32(enable, HAL_GetUIDw1());
    U32(enable, HAL_GetUIDw2());
    enable.push_back(1);
    usb_midi.incoming.push_back(SysEx(Frame(sq::CvEnable, 4, enable)));
    Tick(300);
    CHECK(LastSysEx()[5] == (sq::CvEnable | sq::kResponse));
    CHECK(dac_writes >= 250);
    CHECK(fakeDac.DHR12R2 == 2048 || fakeDac.DHR12R2 == 410);
    // Withdraw the enable: gate-low once, then the DAC stays.
    enable.back() = 0;
    usb_midi.incoming.push_back(SysEx(Frame(sq::CvEnable, 5, enable)));
    Tick(3);
    const unsigned writes = dac_writes;
    CHECK(fakeDac.DHR12R2 == 2048);
    Tick(200);
    CHECK(dac_writes == writes);

    // USB unplugged: the USB table is void, DIN gets its Note-Offs; reconnect: All Notes Off first.
    hUsbDeviceHS.dev_state = 0;
    hw.midi.sent.clear();
    Tick(5);
    CHECK(!fw::din_port.AnySent(1));
    usb_midi.sent.clear();
    Tick(200);
    CHECK(usb_midi.sent.empty());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick();
    CHECK(!usb_midi.sent.empty() && usb_midi.sent[0].size() == 3 && usb_midi.sent[0][0] == 0xB0 &&
          usb_midi.sent[0][1] == 123);

    // MIDI clock over DIN as the source: set clock_source = MIDI (2) over SQ, then Start + clocks.
    std::vector<uint8_t> param = {0, 7, 0};
    U32(param, 2000);
    usb_midi.incoming.push_back(SysEx(Frame(sq::SetParam, 6, param)));
    Tick(2); // main accepts it, the callback applies it at the next block
    CHECK(engine.settings.clock_source == step_seq::ClockSource::Midi);
    hw.tog_sw17.pressed = false;
    Tick(30);
    hw.tog_sw17.pressed = true;
    Tick(30);
    CHECK(engine.GetState() == step_seq::State::Waiting);
    daisy::MidiEvent start;
    start.type = daisy::SystemRealTime;
    start.srt_type = daisy::Start;
    hw.midi.incoming.push_back(start);
    Tick();
    const int before = engine.Position();
    for(int i = 0; i < 24; ++i) {
        daisy::MidiEvent clock;
        clock.type = daisy::SystemRealTime;
        clock.srt_type = daisy::TimingClock;
        hw.midi.incoming.push_back(clock);
        Tick(5);
    }
    CHECK(engine.GetState() == step_seq::State::Running && engine.Position() == 4 && before != 4);
    // Received clock is never forwarded.
    usb_midi.sent.clear();
    hw.midi.sent.clear();
    for(int i = 0; i < 12; ++i) {
        daisy::MidiEvent clock;
        clock.type = daisy::SystemRealTime;
        clock.srt_type = daisy::TimingClock;
        hw.midi.incoming.push_back(clock);
        Tick(5);
    }
    CHECK(Count(usb_midi.sent, 0xF8, 1) == 0 && Count(hw.midi.sent, 0xF8, 1) == 0);

    // Bootloader: S9 + S16 for 5 s -> panic (notes off on both ports, gate low) -> reset.
    param = {0, 7, 0};
    U32(param, 0);
    usb_midi.incoming.push_back(SysEx(Frame(sq::SetParam, 7, param)));
    Tick();
    hw.tog_sw17.pressed = false;
    Tick(30);
    hw.tog_sw17.pressed = true;
    Tick(500);
    CHECK(engine.GetState() == step_seq::State::Running);
    hw.pressed[8] = hw.pressed[15] = true;
    unsigned resets = daisy::System::bootloader_resets;
    for(int i = 0; i < 5200 && daisy::System::bootloader_resets == resets; ++i)
        Tick();
    CHECK(daisy::System::bootloader_resets == resets + 1);
    CHECK(daisy::System::bootloader_mode == daisy::System::DAISY_INFINITE_TIMEOUT);
    CHECK(!engine.GateHigh());
    CHECK(!fw::usb_port.AnySent(1) && !fw::din_port.AnySent(1));
    CHECK(Count(usb_midi.sent, 0xB0) >= 1); // All Notes Off before the reset
    // SysEx bootloader request: acknowledged, then reset.
    hw.pressed[8] = hw.pressed[15] = false;
    Tick(20);
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
    std::printf("PASS: production callback and main loop: power-up on Run, locked/enabled DAC registers, "
                "SQ over USB, DIN clock, USB loss/reconnect, bootloader after panic, no audio allocation\n");
    return 0;
}
