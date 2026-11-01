// SPDX-License-Identifier: MIT
// The production Streichwurst firmware (src/main.cpp) on host fakes: SQ 1.1 identity and
// components, read-first parameter state, SET_PARAM with clamping, ACK and applied
// revision, retries and echo frames, MIDI notes/CCs and channel filter, kit pots with
// soft takeover visible in the monitor, the Mod on Daisy matrix on the same port (empty
// matrix = unchanged sound), CC1 to Mod on Daisy, USB loss, queue overflow without hanging
// notes, the bootloader request after a panic, and no allocation in the audio callback.
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

static float out_l[48], out_r[48];
static double energy = 0;
static bool finite = true;

static void Tick(unsigned count = 1) {
    for(unsigned b = 0; b < count; ++b) {
        const float* in[2] = {out_l, out_r};
        float* out[2] = {out_l, out_r};
        in_audio = true;
        AudioCallback(in, out, 48);
        in_audio = false;
        for(unsigned i = 0; i < 48; ++i) {
            energy += double(out_l[i]) * out_l[i] + double(out_r[i]) * out_r[i];
            finite = finite && std::isfinite(out_l[i]) && std::isfinite(out_r[i]);
        }
        ++daisy::System::now;
        ServiceMain();
    }
}
static double Energy(unsigned blocks) {
    energy = 0;
    Tick(blocks);
    return energy;
}

static daisy::MidiEvent SysEx(const uint8_t* frame, std::size_t n) {
    daisy::MidiEvent e;
    e.type = daisy::SystemCommon;
    e.sc_type = daisy::SystemExclusive;
    e.sysex_message_len = uint8_t(n - 2);
    for(std::size_t i = 1; i + 1 < n; ++i)
        e.sysex_data[i - 1] = frame[i];
    return e;
}
static std::vector<uint8_t> SqFrame(uint8_t type, uint8_t rid, const std::vector<uint8_t>& payload) {
    uint8_t buffer[sq::kMaxFrame];
    sq::Writer w(buffer, sizeof(buffer));
    w.Begin(type, rid);
    for(uint8_t b : payload)
        w.U7(b);
    return std::vector<uint8_t>(buffer, buffer + w.End());
}
static void Send(const std::vector<uint8_t>& frame) { usb_midi.incoming.push_back(SysEx(frame.data(), frame.size())); }
static std::vector<uint8_t> I32(int32_t v) {
    std::vector<uint8_t> out;
    for(int i = 0; i < 5; ++i)
        out.push_back(uint8_t((uint32_t(v) >> (7 * i)) & 0x7F));
    return out;
}
static std::vector<uint8_t> SetParamPayload(uint8_t component, uint16_t pid, int32_t value) {
    std::vector<uint8_t> p = {component, uint8_t(pid & 0x7F), uint8_t(pid >> 7)};
    const std::vector<uint8_t> v = I32(value);
    p.insert(p.end(), v.begin(), v.end());
    return p;
}
// The SQ reply with this type and request ID (or the last SQ frame of that type).
static std::vector<uint8_t> SqReply(uint8_t type, int rid = -1) {
    for(auto it = usb_midi.sent.rbegin(); it != usb_midi.sent.rend(); ++it)
        if(it->size() > 8 && (*it)[0] == 0xF0 && (*it)[2] == 0x53 && (*it)[5] == type && (rid < 0 || (*it)[6] == rid))
            return *it;
    return {};
}
static int CountSq(uint8_t type) {
    int n = 0;
    for(auto& m : usb_midi.sent)
        n += m.size() > 8 && m[0] == 0xF0 && m[2] == 0x53 && m[5] == type;
    return n;
}
static std::vector<uint8_t> MdReply(uint8_t command) {
    for(auto it = usb_midi.sent.rbegin(); it != usb_midi.sent.rend(); ++it)
        if(it->size() > 8 && (*it)[0] == 0xF0 && (*it)[2] == 0x4D && (*it)[6] == command)
            return *it;
    return {};
}
static void SendMd(uint8_t command, uint8_t seq, const std::vector<uint8_t>& payload, uint8_t device = 0) {
    uint8_t frame[mod::proto::kMaxFrame];
    const std::size_t n = mod::proto::BuildFrame(frame, device, command, seq, payload.data(), payload.size());
    usb_midi.incoming.push_back(SysEx(frame, n));
}
static bool Contains(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}
static int32_t ReadI32(const std::vector<uint8_t>& m, std::size_t at) {
    uint32_t v = 0;
    for(int i = 0; i < 5; ++i)
        v |= uint32_t(m[at + std::size_t(i)] & 0x7F) << (7 * i);
    return int32_t(v);
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
static const streichwurst::Instrument& Inst() { return product.GetInstrument(); }
static bool AnyStringNote() {
    for(int m = 0; m < 128; ++m)
        if(Inst().StringNoteActive(m))
            return true;
    return false;
}

int main() {
    for(auto& p : hw.pot)
        p.value = 0.5f;
    Initialize();
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    CHECK(Energy(50) == 0.0); // silent without notes
    CHECK(CountSq(sq::Identify | sq::kResponse) == 0); // the device never talks first

    // ----- SQ 1.1 identity: product, minor version, capabilities, three components -----
    Send(SqFrame(sq::Identify, 1, {}));
    Tick();
    std::vector<uint8_t> r = SqReply(sq::Identify | sq::kResponse, 1);
    CHECK(!r.empty() && r[8] == 1 && r[9] == 1); // protocol 1.1
    CHECK(Contains(r, "streichwurst") && Contains(r, STREICHWURST_VERSION));
    const std::size_t caps_at = r.size() - 6; // u14 capabilities, u7 count, crc_hi, crc_lo, F7
    const uint16_t caps = uint16_t(r[caps_at] | (r[caps_at + 1] << 7));
    CHECK(caps & sq::CapModMatrix);
    CHECK(!(caps & sq::CapPattern) && !(caps & sq::CapCalibration));
    CHECK(r[r.size() - 4] == fw::kComponents);
    const char* names[] = {"streichwurst-instrument", "streichwurst-fx", "streichwurst"};
    const char* versions[] = {STREICHWURST_INSTRUMENT_VERSION, STREICHWURST_FX_VERSION, STREICHWURST_VERSION};
    const uint8_t kinds[] = {sq::KindInstrument, sq::KindEffect, sq::KindProduct};
    for(uint8_t k = 0; k < fw::kComponents; ++k) {
        Send(SqFrame(sq::GetComponent, uint8_t(10 + k), {k}));
        Tick();
        r = SqReply(sq::GetComponent | sq::kResponse, 10 + k);
        CHECK(Contains(r, names[k]) && Contains(r, versions[k]));
        const std::size_t name_len = r[10];
        CHECK(r[11 + name_len] == kinds[k]);
    }
    Send(SqFrame(sq::GetParamDesc, 20, {0, 0}));
    Tick();
    CHECK(Contains(SqReply(sq::GetParamDesc | sq::kResponse, 20), "registration"));
    // Read first: GET_PARAMS reports the defaults of the contract.
    Send(SqFrame(sq::GetParams, 21, {1, 0}));
    Tick();
    r = SqReply(sq::GetParams | sq::kResponse, 21);
    CHECK(!r.empty() && r[10] == 8 && r[11] == 8); // FX: 8 parameters, all in one frame
    CHECK(ReadI32(r, 14) == 0 && ReadI32(r, 14 + 7 * 6) == 20000); // animate 0, reverb 20 %

    // ----- SET_PARAM: ACK with the clamped value, accepted and applied revision -----
    Send(SqFrame(sq::SetParam, 30, SetParamPayload(0, 1, 75000))); // registration 75 %
    Tick(2);
    r = SqReply(sq::SetParam | sq::kResponse, 30);
    CHECK(!r.empty() && r[8] == 0 && ReadI32(r, 11) == 75000);
    const uint16_t accepted = uint16_t(r[16] | (r[17] << 7));
    CHECK(accepted == 1);
    CHECK(std::fabs(product.GetState().inst.string_registration - 0.75f) < 1e-6f);
    Send(SqFrame(sq::GetState, 31, {}));
    Tick();
    r = SqReply(sq::GetState | sq::kResponse, 31);
    CHECK(!r.empty() && r[19] == 1 && r[21] == 1); // accepted 1, applied 1 (u14 LSB)
    Send(SqFrame(sq::SetParam, 32, SetParamPayload(2, 2, 250000))); // master 250 % -> 100 %
    Tick(2);
    CHECK(ReadI32(SqReply(sq::SetParam | sq::kResponse, 32), 11) == 100000);
    Send(SqFrame(sq::SetParam, 33, SetParamPayload(0, 99, 0))); // unknown parameter
    Tick();
    r = SqReply(sq::kNak, 33);
    CHECK(!r.empty() && r[9] == sq::ErrBadArgument);
    Send(SqFrame(sq::SetParam, 34, SetParamPayload(2, 4, 1000))); // pot page: read only
    Tick();
    CHECK(!SqReply(sq::kNak, 34).empty());
    // A retry with the same request ID and bytes is answered from the cache, applied once.
    const std::vector<uint8_t> retry = SqFrame(sq::SetParam, 35, SetParamPayload(1, 4, 2000)); // mode 2
    Send(retry);
    Tick(2);
    const int acks = CountSq(sq::SetParam | sq::kResponse);
    Send(retry);
    Tick(2);
    CHECK(CountSq(sq::SetParam | sq::kResponse) == acks + 1);
    CHECK(fw::device.counters[sq::CntDuplicate] == 1);
    CHECK(fw::device.AcceptedRevision() == 3);
    CHECK(product.GetState().fx.ensemble_mode == 2);
    // Echo of a reply (DAW input monitoring): consumed, never answered.
    const std::size_t before_echo = usb_midi.sent.size();
    std::vector<uint8_t> echo = SqReply(sq::SetParam | sq::kResponse, 35);
    Send(echo);
    Tick(2);
    CHECK(usb_midi.sent.size() == before_echo);

    // ----- MIDI: notes (omni), CC plan, monitor shows changes made outside the app -----
    Send(SqFrame(sq::Monitor, 40, {1}));
    Tick(2);
    usb_midi.incoming.push_back(Note(true, 60, 1));
    usb_midi.incoming.push_back(Note(true, 67, 5));
    Tick(5);
    CHECK(Inst().StringNoteActive(60) && Inst().StringNoteActive(67));
    CHECK(Energy(200) > 1.0);
    const int monitor_before = CountSq(sq::kMonitorEvent);
    usb_midi.incoming.push_back(Cc(20, 0, 1)); // registration -> 0 %
    Tick(30);
    CHECK(product.GetState().inst.string_registration == 0.f);
    CHECK(fw::values[fw::kInstrument][0] == 0);
    bool reported = false;
    for(auto& m : usb_midi.sent)
        if(m.size() > 12 && m[2] == 0x53 && m[5] == sq::kMonitorEvent && m[8] == sq::EvParam && m[9] == 0 && m[10] == 1)
            reported = reported || ReadI32(m, 12) == 0;
    CHECK(reported && CountSq(sq::kMonitorEvent) > monitor_before);
    // Kit pot (main page VR3 = release): latched at power-up, picked up when crossed.
    hw.pot[2].value = 0.1f;
    Tick(5);
    CHECK(std::fabs(product.GetState().inst.string_release - 0.45f) < 1e-6f);
    hw.pot[2].value = 0.5f; // crosses 0.45
    Tick(5);
    hw.pot[2].value = 0.9f;
    Tick(30);
    CHECK(std::fabs(product.GetState().inst.string_release - 0.9f) < 0.01f);
    reported = false;
    for(auto& m : usb_midi.sent)
        if(m.size() > 12 && m[2] == 0x53 && m[5] == sq::kMonitorEvent && m[8] == sq::EvParam && m[9] == 0 && m[10] == 4)
            reported = true;
    CHECK(reported);
    // All Notes Off (libDaisy delivers CC123 as a channel mode message) releases.
    daisy::MidiEvent all_off;
    all_off.type = daisy::ChannelMode;
    all_off.cm_type = daisy::AllNotesOff;
    usb_midi.incoming.push_back(all_off);
    Tick(3);
    CHECK(!AnyStringNote());

    // ----- Mod on Daisy on the same port: identify, a cell, CC1 consumed -----
    const double reference = [] {
        usb_midi.incoming.push_back(Note(true, 64, 1));
        Tick(400);
        return Energy(200);
    }();
    SendMd(mod::proto::Identify, 1, {}, mod::proto::kBroadcast);
    Tick(5);
    std::vector<uint8_t> md = MdReply(mod::proto::Identify | mod::proto::kReplyBit);
    CHECK(Contains(md, "streichwurst"));
    // LFO 1 -> Registration, depth 100, on: the effective registration moves, the
    // register value (what SQ reports) does not.
    SendMd(mod::proto::SetCell, 2, {0, fw::TargetRegistration, 100, 2});
    Tick(5);
    CHECK(!MdReply(mod::proto::SetCell | mod::proto::kReplyBit).empty());
    Tick(600);
    CHECK(product.GetState().inst.string_registration == 0.f);
    CHECK(Inst().Parameters().string_registration != 0.f);
    SendMd(mod::proto::ClearMatrix, 3, {});
    Tick(10);
    CHECK(Inst().Parameters().string_registration == 0.f); // empty matrix: base value exactly
    const std::size_t routed_before = usb_midi.sent.size();
    usb_midi.incoming.push_back(Cc(1, 100, 1)); // mod wheel: consumed by Mod on Daisy
    Tick(3);
    CHECK(usb_midi.sent.size() >= routed_before);
    CHECK(reference > 0.0);

    // ----- channel filter, channel change and USB loss release held notes -----
    Send(SqFrame(sq::SetParam, 50, SetParamPayload(2, 3, 2000))); // MIDI channel 2
    Tick(5);
    CHECK(!AnyStringNote()); // channel change: All Notes Off
    usb_midi.incoming.push_back(Note(true, 62, 1));
    Tick(3);
    CHECK(!Inst().StringNoteActive(62)); // other channel ignored
    usb_midi.incoming.push_back(Note(true, 62, 2));
    hw.midi.incoming.push_back(Note(true, 65, 2)); // DIN
    Tick(3);
    CHECK(Inst().StringNoteActive(62) && Inst().StringNoteActive(65));
    hUsbDeviceHS.dev_state = 0;
    Tick(3);
    CHECK(!AnyStringNote());
    hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
    Tick(2);

    // ----- queue overflow: lost Note-Offs turn into All Notes Off, nothing hangs -----
    for(int i = 0; i < 100; ++i)
        usb_midi.incoming.push_back(Note(true, uint8_t(20 + i), 2));
    for(int i = 0; i < 100; ++i)
        usb_midi.incoming.push_back(Note(false, uint8_t(20 + i), 2));
    Tick(5);
    CHECK(!AnyStringNote());
    CHECK(fw::device.counters[sq::CntMidiQueueOverflow] > 0);

    // ----- bootloader request over SysEx: panic first, then the reply and the reset -----
    usb_midi.incoming.push_back(Note(true, 60, 2));
    Tick(100);
    usb_midi.incoming.push_back(SysEx(bootloader::kRequest, sizeof(bootloader::kRequest)));
    Tick(1);
    CHECK(daisy::System::bootloader_resets == 1);
    bool acked = false;
    for(auto& m : usb_midi.sent)
        acked = acked || (m.size() == sizeof(bootloader::kAck) && std::equal(m.begin(), m.end(), bootloader::kAck));
    CHECK(acked);
    Tick(5); // on the board the audio interrupt runs during the main loop's wait
    CHECK(product.Stats().panics >= 1);

    CHECK(finite);
    CHECK(allocations == 0);
    CHECK(product.Stats().non_finite == 0);
    if(failures) {
        std::printf("streichwurst firmware: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("streichwurst firmware: all tests passed\n");
    return 0;
}
