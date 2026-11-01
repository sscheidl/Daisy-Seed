// Host tests for the firmware main loop (diag::App) with fake board and USB sink.
// Covers the paths that main.cpp only wires to hardware: command/response flow,
// streaming, BYE, host timeout, RX ring, double-buffered TX and LED/SD requests.
#include "diag_app.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;
int checks = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++checks;                                                               \
        if(!(cond)) {                                                           \
            ++failures;                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                       \
    } while(0)

class FakeUsb final : public diag::UsbSink {
  public:
    diag::SendResult mode = diag::SendResult::Ok;
    std::string delivered;
    int sends = 0;
    bool buffer_reused_in_flight = false;

    diag::SendResult Send(const uint8_t* data, size_t len) override {
        ++sends;
        if(mode != diag::SendResult::Ok)
            return mode;
        // The previously submitted buffer must not have been modified meanwhile.
        if(last_ptr_ && std::memcmp(last_ptr_, last_copy_.data(), last_copy_.size()) != 0)
            buffer_reused_in_flight = true;
        last_ptr_ = data;
        last_copy_.assign(data, data + len);
        delivered.append(reinterpret_cast<const char*>(data), len);
        return diag::SendResult::Ok;
    }

  private:
    const uint8_t* last_ptr_ = nullptr;
    std::vector<uint8_t> last_copy_;
};

class FakeBoard final : public diag::Board {
  public:
    diag::Inputs inputs;
    uint32_t ticks = 0;
    std::vector<std::pair<uint16_t, uint8_t>> led_writes;
    std::vector<std::pair<uint16_t, uint16_t>> cv_writes;
    std::vector<bool> sd_calls;
    diag::SdResult sd_result;
    uint32_t* clock = nullptr;  // RunSdTest advances it to model a blocking test
    uint32_t sd_block_ms = 0;
    unsigned bootloader_entries = 0;
    size_t delivered_at_boot = 0;
    const FakeUsb* usb = nullptr;

    diag::Inputs ReadInputs() override { return inputs; }
    uint32_t ControlTicks() override { return ticks; }
    void ApplyLeds(uint16_t leds, uint8_t br) override { led_writes.emplace_back(leds, br); }
    void ApplyCvOut(uint16_t c1, uint16_t c2) override { cv_writes.emplace_back(c1, c2); }
    diag::SdResult RunSdTest(bool card) override {
        sd_calls.push_back(card);
        if(clock)
            *clock += sd_block_ms;
        return sd_result;
    }
    void EnterBootloader() override {
        ++bootloader_entries;
        if(usb)
            delivered_at_boot = usb->delivered.size();
    }

    // ---- audio (0.4.0) ----
    bool audio = false;
    diag::ToneSettings tone;
    uint32_t tone_until = 0;
    int tone_writes = 0;
    diag::AudioLevels levels;
    int level_reads = 0;
    uint16_t scope_frames = 0;
    bool scope_trigger = false;
    bool scope_ready = false;
    int scope_starts = 0;
    std::vector<int16_t> scope_samples;

    bool AudioSupported() override { return audio; }
    void ApplyTone(const diag::ToneSettings& t, uint32_t keep_until) override {
        tone = t;
        tone_until = keep_until;
        ++tone_writes;
    }
    diag::AudioLevels ReadAudioLevels() override {
        ++level_reads;
        const diag::AudioLevels read = levels;
        levels = diag::AudioLevels();
        return read;
    }
    bool StartScope(uint16_t frames, bool trigger) override {
        if(scope_frames && !scope_ready)
            return false;  // still capturing
        ++scope_starts;
        scope_frames = frames;
        scope_trigger = trigger;
        scope_ready = false;
        return true;
    }
    const int16_t* ScopeData(uint16_t& frames) override {
        if(!scope_ready) {
            frames = 0;
            return nullptr;
        }
        frames = scope_frames;
        return scope_samples.data();
    }
};

struct Rig {
    FakeBoard board;
    FakeUsb usb;
    uint32_t now = 1000;
    diag::App app{board, usb, "v6_1", 1000};
    ddp::FrameParser parser;
    size_t consumed = 0;

    void Host(const std::string& body) {
        char frame[ddp::kMaxFrame];
        const size_t n = ddp::BuildFrame(body.data(), body.size(), frame, sizeof(frame));
        app.OnUsbReceive(reinterpret_cast<const uint8_t*>(frame), static_cast<uint32_t>(n));
    }
    // Advances time in 1 ms main-loop steps (control ticks follow at 1 kHz).
    void Run(uint32_t ms) {
        for(uint32_t i = 0; i < ms; ++i) {
            ++now;
            ++board.ticks;
            app.Step(now);
        }
    }
    // Bodies delivered to the host since the previous call.
    std::vector<std::string> Received() {
        std::vector<std::string> out;
        for(; consumed < usb.delivered.size(); ++consumed)
            if(parser.Feed(usb.delivered[consumed]))
                out.emplace_back(parser.Body());
        return out;
    }
};

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

int Count(const std::vector<std::string>& bodies, const char* prefix) {
    int n = 0;
    for(const auto& b : bodies)
        n += StartsWith(b, prefix);
    return n;
}

void TestSilentUntilHello() {
    Rig r;
    r.Run(500);
    CHECK(r.Received().empty());
    r.Host("PING n=5");
    r.Run(1);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "PONG n=5");
    r.Run(100);
    CHECK(r.Received().empty());  // PING alone does not start streaming
}

void TestHandshakeAndRate() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(1);
    auto got = r.Received();
    CHECK(got.size() == 3);
    CHECK(got.size() == 3 && StartsWith(got[0], "HELLO proto=1 fw=0.4.1"));
    CHECK(got.size() == 3 && StartsWith(got[1], "ST n=0 "));
    CHECK(got.size() == 3 && StartsWith(got[2], "SYS "));
    for(int k = 0; k < 4; ++k) {  // keep the host alive with PINGs
        r.Run(250);
        r.Host("PING n=1");
    }
    r.Run(1);
    got = r.Received();
    const int st = Count(got, "ST ");
    CHECK(st >= 49 && st <= 51);  // 50 Hz default
    CHECK(Count(got, "SYS ") == 1);
    // sequence numbers are consecutive
    uint32_t expected = 1;
    for(const auto& b : got) {
        if(!StartsWith(b, "ST "))
            continue;
        char n[12];
        uint32_t seq = 0;
        CHECK(ddp::FindField(b.c_str(), "n", n, sizeof(n)) && ddp::ParseU32(n, seq));
        CHECK(seq == expected);
        expected = seq + 1;
    }
    CHECK(r.app.stats().control_rate_hz == 1000);
}

void TestImmediateFrameOnChange() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(1);
    r.Received();
    r.Run(5);  // well inside the 20 ms period
    r.board.inputs.buttons = 0x0010;
    r.board.inputs.press_count[4] = 1;
    r.Run(1);  // 6 ms after the last frame
    auto got = r.Received();
    CHECK(Count(got, "ST ") == 0);  // minimum frame spacing of 10 ms is respected
    r.Run(4);  // 10 ms: change frame, well before the periodic one at 20 ms
    got = r.Received();
    CHECK(Count(got, "ST ") == 1);
    CHECK(!got.empty() && got[0].find("b=0010") != std::string::npos);
}

void TestByeIsAcknowledged() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Received();
    r.Host("BYE");
    r.Run(1);
    auto got = r.Received();
    CHECK(!got.empty() && got.back() == "ACK c=BYE");
    CHECK(r.app.stats().tx_drop == 0);
    r.Run(200);
    CHECK(r.Received().empty());
}

void TestTimeoutNeedsNewHello() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(2990);
    CHECK(Count(r.Received(), "ST ") > 100);
    r.Run(20);  // 3 s without a host frame
    r.Received();
    r.Run(100);
    CHECK(r.Received().empty());
    r.Host("PING n=9");
    r.Run(100);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "PONG n=9");  // answered, but no stream
    r.Host("HELLO proto=1");
    r.Run(100);
    got = r.Received();
    CHECK(!got.empty() && StartsWith(got[0], "HELLO "));
    CHECK(Count(got, "ST ") >= 5);
}

void TestNotReadyDiscards() {
    Rig r;
    r.usb.mode = diag::SendResult::NotReady;  // e.g. powered from a charger, no host
    r.Host("PING n=1");
    r.Run(10);
    CHECK(r.usb.delivered.empty());
    CHECK(r.app.stats().tx_drop == 1);
    r.usb.mode = diag::SendResult::Ok;
    r.Host("PING n=2");
    r.Run(1);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "PONG n=2");
}

void TestBusyEndpointBuffersAndDrops() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(1);
    r.Received();
    r.usb.mode = diag::SendResult::Busy;  // host stopped reading
    for(int k = 0; k < 8; ++k) {
        r.Run(250);
        r.Host("PING n=1");
    }
    CHECK(r.usb.delivered.size() == r.consumed);
    CHECK(r.app.stats().tx_busy > 1000);
    CHECK(r.app.stats().tx_drop > 0);  // 1536-byte buffer full
    r.usb.mode = diag::SendResult::Ok;
    r.Run(100);
    auto got = r.Received();
    CHECK(Count(got, "ST ") >= 5);
    for(int k = 0; k < 20; ++k)
        r.Run(50);
    CHECK(!r.usb.buffer_reused_in_flight);
    CHECK(r.parser.bad_crc == 0);
}

void TestNewSessionDropsStaleOutput() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(1);
    r.Received();
    r.usb.mode = diag::SendResult::Busy;  // previous host closed the port without BYE
    r.Run(200);
    const uint32_t dropped_before = r.app.stats().tx_drop;
    r.usb.mode = diag::SendResult::Ok;
    r.Host("HELLO proto=1");  // in the same step the stale frames would be sent
    r.Run(1);
    auto got = r.Received();
    CHECK(!got.empty() && StartsWith(got[0], "HELLO proto=1"));
    CHECK(r.app.stats().tx_drop == dropped_before + 1);
}

void TestRxOverflowAndResync() {
    Rig r;
    std::string junk(3000, 'x');
    r.app.OnUsbReceive(reinterpret_cast<const uint8_t*>(junk.data()),
                       static_cast<uint32_t>(junk.size()));
    r.Run(1);
    CHECK(r.app.stats().rx_bad >= 3000 - diag::App::kRxSize);
    r.Host("PING n=77");
    r.Run(1);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "PONG n=77");
    r.Host("NOPE");
    r.Run(1);
    got = r.Received();
    CHECK(got.size() == 1 && got[0] == "ERR c=NOPE e=UNKNOWN");
}

void TestRingWrapAndFull() {
    diag::SpscRing<16> ring(0xfffffff0u);  // free-running indices wrap during the test
    uint8_t value = 0;
    uint8_t expect = 0;
    for(int round = 0; round < 40; ++round) {
        uint8_t block[5];
        for(auto& b : block)
            b = value++;
        CHECK(ring.Push(block, sizeof(block)) == sizeof(block));
        uint8_t out = 0;
        for(int k = 0; k < 5; ++k) {
            CHECK(ring.Pop(out));
            CHECK(out == expect);
            ++expect;
        }
        CHECK(ring.Size() == 0);
    }
    uint8_t many[20] = {};
    CHECK(ring.Push(many, sizeof(many)) == 16);
    CHECK(ring.Size() == 16 && ring.Overflow() == 4);
    CHECK(ring.Push(many, 1) == 0);
    CHECK(ring.Overflow() == 5);
    uint8_t out;
    int popped = 0;
    while(ring.Pop(out))
        ++popped;
    CHECK(popped == 16 && !ring.Pop(out));
}

void TestLeds() {
    Rig r;
    r.Run(1);
    CHECK(r.board.led_writes.size() == 1 && r.board.led_writes[0].first == 0);  // initial
    r.board.inputs.buttons = 0x0001;  // FOLLOW is the power-up default
    r.Run(1);
    CHECK(r.board.led_writes.size() == 1);  // rate limited to one write per 10 ms
    r.Run(10);
    CHECK(r.board.led_writes.size() == 2 && r.board.led_writes.back().first == 0x0001);
    r.Host("LED m=MASK mask=8001 br=40");
    r.Run(20);
    CHECK(r.board.led_writes.back() == std::make_pair(uint16_t{0x8001}, uint8_t{40}));
    const size_t writes = r.board.led_writes.size();
    r.Run(500);
    CHECK(r.board.led_writes.size() == writes);  // unchanged state is not rewritten
    r.Host("LED m=CHASE ms=20");
    r.Run(400);
    CHECK(r.board.led_writes.size() >= writes + 19);
    r.Host("LED m=ON br=100000000000");  // rejected as a whole
    r.Run(20);
    CHECK(r.app.settings().led_mode == diag::LedMode::Chase);
}

void TestSdRequest() {
    Rig r;
    r.Host("SDTEST");
    r.Run(1);
    CHECK(r.board.sd_calls.size() == 1 && !r.board.sd_calls[0]);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "SD st=NONE res=-1");  // answered without HELLO
    r.board.inputs.sd_card = true;
    r.board.sd_result.status = diag::SdStatus::Mounted;
    r.board.sd_result.fresult = 0;
    r.board.sd_result.fs_type = "FAT32";
    r.board.sd_result.size_mb = 100;
    r.board.sd_result.entries = 3;
    r.Host("SDTEST");
    r.Run(1);
    CHECK(r.board.sd_calls.size() == 2 && r.board.sd_calls[1]);
    got = r.Received();
    CHECK(got.size() == 1 && got[0] == "SD st=MOUNTED res=0 fs=FAT32 mb=100 ent=3");
}
void TestBlockingSdTestKeepsStream() {
    // The main loop is blocked for 6 s inside the SD test. Keep-alives sent by the host
    // meanwhile are queued and processed afterwards; the stream must continue.
    Rig r;
    r.board.clock = &r.now;
    r.board.sd_block_ms = 6000;
    r.board.inputs.sd_card = true;
    r.Host("HELLO proto=1");
    r.Run(100);
    r.Received();
    r.Host("SDTEST");
    r.Run(1);  // blocks: now jumps by 6 s
    for(int k = 0; k < 12; ++k)
        r.Host("PING n=1");  // queued in the RX ring while blocked
    r.Run(200);
    auto got = r.Received();
    CHECK(Count(got, "SD st=") == 1);
    CHECK(Count(got, "PONG ") == 12);
    CHECK(Count(got, "ST ") >= 9);
    CHECK(r.app.HostActive(r.now));
}
} // namespace

// BOOT: only in a session, answered first, then exactly one reset into the bootloader.
void TestBootloader() {
    Rig r;
    r.board.usb = &r.usb;
    r.Host("BOOT");
    r.Run(5);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "ERR c=BOOT e=SESSION");
    CHECK(r.board.bootloader_entries == 0);
    r.Host("HELLO proto=1");
    r.Run(30);
    r.Received();
    r.Host("BOOT");
    r.Run(1);
    CHECK(r.board.bootloader_entries == 1);
    got = r.Received();
    CHECK(Count(got, "ACK c=BOOT") == 1);
    // The ACK had been handed to USB before the reset was requested.
    CHECK(r.board.delivered_at_boot == r.usb.delivered.size());
    r.Run(100);
    CHECK(r.board.bootloader_entries == 1);
}

// The key gesture of every app: S9+S16 alone for 5 s, also without a host.
void TestBootloaderKeys() {
    Rig r;
    r.board.inputs.buttons = (1u << 8) | (1u << 15) | 1u;  // with S1: never
    r.Run(6000);
    CHECK(r.board.bootloader_entries == 0);
    r.board.inputs.buttons = (1u << 8) | (1u << 15);
    r.Run(4999);
    CHECK(r.board.bootloader_entries == 0);
    r.Run(2);
    CHECK(r.board.bootloader_entries == 1);
    r.Run(10000);  // still held: once
    CHECK(r.board.bootloader_entries == 1);
}

void TestCvOutLifecycle() {
    Rig r;
    r.Run(1);
    CHECK(r.board.cv_writes.size() == 1 && r.board.cv_writes[0] == std::make_pair(
                                                 uint16_t{0}, uint16_t{0}));  // OFF at start
    r.Host("CVOUT m=FIX c1=7");  // no session: rejected, nothing changes
    r.Run(5);
    auto got = r.Received();
    CHECK(got.size() == 1 && got[0] == "ERR c=CVOUT e=SESSION");
    CHECK(r.board.cv_writes.size() == 1);

    r.Host("HELLO proto=1");
    r.Host("CVOUT m=FIX c1=1000 c2=2000");
    r.Run(30);
    CHECK(r.board.cv_writes.back() == std::make_pair(uint16_t{1000}, uint16_t{2000}));
    const size_t writes = r.board.cv_writes.size();
    r.Run(100);
    CHECK(r.board.cv_writes.size() == writes);  // unchanged codes are not rewritten
    got = r.Received();
    bool seen = false;
    for(const auto& b : got)
        seen = seen || (StartsWith(b, "ST ") && b.find(" co=FIX,1000,2000") != std::string::npos);
    CHECK(seen);

    r.Host("CVOUT m=SAW ms=100");
    r.Run(100);
    CHECK(r.board.cv_writes.size() > writes + 50);  // the saw is updated every step

    r.Host("BYE");  // end of session: back to 0 V code
    r.Run(2);
    CHECK(r.board.cv_writes.back() == std::make_pair(uint16_t{0}, uint16_t{0}));
    CHECK(r.app.settings().cv_mode == diag::CvOutMode::Off);

    r.Host("HELLO proto=1");
    r.Host("CVOUT m=FIX c1=4095 c2=4095");
    r.Run(10);
    CHECK(r.board.cv_writes.back() == std::make_pair(uint16_t{4095}, uint16_t{4095}));
    r.Run(3100);  // host silent for more than 3 s: timeout switches CV OUT off as well
    CHECK(r.board.cv_writes.back() == std::make_pair(uint16_t{0}, uint16_t{0}));
}

void TestGatesInState() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(1);
    auto got = r.Received();
    CHECK(Count(got, "ST ") == 1 && got[1].find(" g=00 gc=0000 ") != std::string::npos);
    r.Run(10);
    r.Received();
    r.board.inputs.gates = 1;
    r.board.inputs.gate_count[0] = 3;
    r.board.inputs.cv_norm[1] = -2500;
    r.board.inputs.cv_raw[1] = 24575;
    r.Run(1);  // gate change: immediate frame (spacing >= 10 ms already given)
    got = r.Received();
    CHECK(Count(got, "ST ") == 1);
    CHECK(!got.empty() && got[0].find(" cv=0,-2500 cvr=0,24575 g=10 gc=0300 ") != std::string::npos);
}

// ---- audio extension (firmware 0.4.0) ---------------------------------------------
void TestToneLifecycle() {
    Rig r;
    r.board.audio = true;
    r.Host("TONE m=SINE");
    r.Run(5);
    auto got = r.Received();
    CHECK(Count(got, "ERR c=TONE e=SESSION") == 1);
    CHECK(r.board.tone.mode == diag::ToneMode::Off);
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Received();
    r.Host("TONE m=SINE f=1000 a=2000");
    r.Run(5);
    got = r.Received();
    CHECK(Count(got, "ACK c=TONE m=SINE f=1000 a=2000 ch=LR") == 1);
    CHECK(r.board.tone.mode == diag::ToneMode::Sine && r.board.tone.amplitude == 2000);
    CHECK(r.board.tone_until > r.now && r.board.tone_until <= r.now + diag::App::kToneKeepAliveMs);
    const uint32_t before = r.board.tone_until;
    r.Run(300);
    CHECK(r.board.tone_until > before);  // the running main loop refreshes the deadline
    r.board.levels.in[0] = 1234;
    r.board.levels.out[1] = 2000;
    r.Run(100);
    got = r.Received();
    CHECK(Count(got, "AU ") >= 1);
    bool levels_seen = false;
    for(const auto& b : got)
        if(b.find("il=1234 ") != std::string::npos && b.find(" or=2000 ") != std::string::npos
           && b.find("tone=SINE,1000,2000,LR") != std::string::npos)
            levels_seen = true;
    CHECK(levels_seen);
    CHECK(r.board.level_reads >= 1);
    // Session end: the tone is switched off and no AU frames follow.
    r.Host("BYE");
    r.Run(5);
    CHECK(r.board.tone.mode == diag::ToneMode::Off);
    r.Received();
    r.Run(500);
    CHECK(Count(r.Received(), "AU ") == 0);
    // Timeout as well.
    r.Host("HELLO proto=1");
    r.Host("TONE m=SINE");
    r.Run(5);
    CHECK(r.board.tone.mode == diag::ToneMode::Sine);
    r.Run(diag::kHostTimeoutMs + 10);
    CHECK(r.board.tone.mode == diag::ToneMode::Off);
}

void TestOutputsSilencedBeforeBlockingSdTest() {
    Rig r;
    r.board.audio = true;
    r.board.inputs.sd_card = true;
    r.board.clock = &r.now;
    r.board.sd_block_ms = 3000;
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Host("CVOUT m=FIX c1=4000 c2=1");
    r.Host("TONE m=SINE");
    r.Run(5);
    r.Received();
    CHECK(!r.board.cv_writes.empty());
    const auto applied = r.board.cv_writes.back();
    CHECK(applied.first == 4000 && applied.second == 1);
    r.board.cv_writes.clear();
    r.Host("SDTEST");
    r.Run(1);  // blocks: CV OUT and tone must be off before the test starts
    CHECK(!r.board.cv_writes.empty());
    const auto silenced = r.board.cv_writes.front();
    CHECK(silenced.first == 0 && silenced.second == 0);
    CHECK(r.board.tone.mode == diag::ToneMode::Off);
    CHECK(r.app.settings().cv_mode == diag::CvOutMode::Off);
    CHECK(Count(r.Received(), "SD st=") == 1);
    // Without queued host frames the blocked time counts as silence: no artificial keep-alive.
    r.Run(10);
    CHECK(!r.app.HostActive(r.now));
}

void TestScopeTransfer() {
    Rig r;
    r.board.audio = true;
    r.Host("SCOPE");
    r.Run(2);
    auto got = r.Received();
    CHECK(Count(got, "ERR c=SCOPE e=SESSION") == 1);
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Received();
    r.Host("SCOPE n=256 t=0");
    r.Run(2);
    got = r.Received();
    CHECK(Count(got, "ACK c=SCOPE n=256 t=0 k=7") == 1);
    CHECK(r.board.scope_starts == 1 && r.board.scope_frames == 256 && !r.board.scope_trigger);
    r.Host("SCOPE");
    r.Run(2);
    got = r.Received();
    CHECK(Count(got, "ERR c=SCOPE e=BUSY") == 1);
    CHECK(Count(got, "SC ") == 0);  // nothing until the capture is complete
    r.board.scope_samples.assign(512, 0);
    for(int i = 0; i < 256; ++i) {
        r.board.scope_samples[2 * i] = static_cast<int16_t>(i);
        r.board.scope_samples[2 * i + 1] = static_cast<int16_t>(-i);
    }
    r.board.scope_ready = true;
    r.Run(50);
    got = r.Received();
    CHECK(Count(got, "SC ") == 7);
    int expected_index = 0;
    size_t hex_digits = 0;
    for(const auto& b : got) {
        if(!StartsWith(b, "SC "))
            continue;
        char v[12];
        uint32_t index = 999;
        CHECK(ddp::FindField(b.c_str(), "i", v, sizeof(v)) && ddp::ParseU32(v, index));
        CHECK(static_cast<int>(index) == expected_index++);
        CHECK(ddp::FindField(b.c_str(), "k", v, sizeof(v)) && std::string(v) == "7");
        CHECK(ddp::FindField(b.c_str(), "n", v, sizeof(v)) && std::string(v) == "256");
        const size_t d = b.find(" d=");
        CHECK(d != std::string::npos && b.size() <= ddp::kMaxBody);
        hex_digits += b.size() - (d + 3);
    }
    CHECK(hex_digits == 256u * 8u);  // every stereo frame exactly once
    CHECK(Count(got, "ST ") >= 1);  // the state stream keeps flowing between chunks
    CHECK(r.app.stats().tx_drop == 0);
    r.Host("SCOPE n=512");  // accepted again after the transfer
    r.Run(2);
    got = r.Received();
    CHECK(Count(got, "ACK c=SCOPE n=512 t=1 k=13") == 1);
    CHECK(r.board.scope_starts == 2);
}

void TestScopeUnsupportedBoard() {
    Rig r;  // protected/default board without audio
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Received();
    r.Host("SCOPE");
    r.Run(2);
    auto got = r.Received();
    CHECK(Count(got, "ERR c=SCOPE e=UNSUPPORTED") == 1);
    r.Run(300);
    CHECK(Count(r.Received(), "AU ") == 0);
    CHECK(r.board.tone_writes == 0 || r.board.tone.mode == diag::ToneMode::Off);
}

void TestScopeBackpressurePreservesStatusAndReplies() {
    for(const uint16_t frames : {uint16_t{256}, uint16_t{2048}}) {
        Rig r;
        r.board.audio = true;
        r.Host("HELLO proto=1");
        r.Run(16);
        r.Received();
        r.Host("SCOPE n=" + std::to_string(frames) + " t=0");
        r.Run(1);
        CHECK(Count(r.Received(), "ACK c=SCOPE ") == 1);
        r.board.scope_samples.assign(static_cast<size_t>(frames) * 2, 0);
        r.board.scope_ready = true;

        // A short busy interval crosses the next 50 Hz status deadline. Bulk
        // snapshot traffic must leave room for that status and a host reply.
        r.usb.mode = diag::SendResult::Busy;
        r.Run(5);
        r.Host("PING n=123");
        r.Run(5);
        r.usb.mode = diag::SendResult::Ok;
        r.Run(100);
        const auto got = r.Received();
        CHECK(r.app.stats().tx_drop == 0);
        CHECK(!r.usb.buffer_reused_in_flight);
        CHECK(Count(got, "PONG n=123") == 1);
        CHECK(Count(got, "ST ") >= 5);
        const int chunks = static_cast<int>((frames + 39) / 40);
        CHECK(Count(got, "SC ") == chunks);
        uint32_t expected_seq = 1;
        uint32_t expected_chunk = 0;
        size_t hex_digits = 0;
        for(const auto& body : got) {
            char text[12];
            uint32_t value = 0;
            if(StartsWith(body, "ST ")) {
                CHECK(ddp::FindField(body.c_str(), "n", text, sizeof(text))
                      && ddp::ParseU32(text, value));
                CHECK(value == expected_seq++);
            } else if(StartsWith(body, "SC ")) {
                CHECK(ddp::FindField(body.c_str(), "i", text, sizeof(text))
                      && ddp::ParseU32(text, value));
                CHECK(value == expected_chunk++);
                const size_t data = body.find(" d=");
                CHECK(data != std::string::npos);
                if(data != std::string::npos)
                    hex_digits += body.size() - (data + 3);
            }
        }
        CHECK(hex_digits == static_cast<size_t>(frames) * 8);
    }
}

void TestPotDimBrightness() {
    Rig r;
    r.Host("HELLO proto=1");
    r.Run(50);
    r.Host("LED m=POTDIM");
    r.board.inputs.pot_norm[0] = 5000;
    r.Run(20);
    const auto& writes = r.board.led_writes;
    CHECK(!writes.empty() && writes.back().first == 0xffff && writes.back().second == 50);
    r.board.inputs.pot_norm[0] = 10000;
    r.Run(20);
    CHECK(writes.back().second == 100);
    auto got = r.Received();
    bool reported = false;
    for(const auto& b : got)
        if(StartsWith(b, "ST ") && b.find("lm=POTDIM") != std::string::npos
           && b.find(" br=100") != std::string::npos)
            reported = true;
    CHECK(reported);
    r.Host("LED m=FOLLOW br=30");
    r.Run(20);
    CHECK(writes.back().second == 30);
}

int main() {
    TestBootloader();
    TestBootloaderKeys();
    TestToneLifecycle();
    TestOutputsSilencedBeforeBlockingSdTest();
    TestScopeTransfer();
    TestScopeBackpressurePreservesStatusAndReplies();
    TestScopeUnsupportedBoard();
    TestPotDimBrightness();
    TestSilentUntilHello();
    TestHandshakeAndRate();
    TestImmediateFrameOnChange();
    TestByeIsAcknowledged();
    TestTimeoutNeedsNewHello();
    TestNotReadyDiscards();
    TestBusyEndpointBuffersAndDrops();
    TestNewSessionDropsStaleOutput();
    TestRxOverflowAndResync();
    TestRingWrapAndFull();
    TestLeds();
    TestSdRequest();
    TestBlockingSdTestKeepsStream();
    TestCvOutLifecycle();
    TestGatesInState();
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
