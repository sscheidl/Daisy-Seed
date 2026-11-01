// SPDX-License-Identifier: MIT
// Looper SD management personality: DDP session, card ownership commands, return to
// performance only from LOCAL, key gesture, LEDs. Fake USB/CDC and card. Same cases
// as Nebulae's tests/test_admin.cpp, with the LOOPER message.
#include "sd_admin.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace looper::mgmt;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while(0)

struct Card final : diag::CardDevice {
    bool open_ok = true, sync_ok = true;
    unsigned closes = 0;
    bool Open(uint32_t& s) override { s = 1000; return open_ok; }
    bool Read(uint32_t, uint8_t*) override { return true; }
    bool Write(uint32_t, const uint8_t*) override { return true; }
    bool Sync() override { return sync_ok; }
    void Close() override { ++closes; }
};
struct Port final : AdminPort {
    AdminInputs in;
    bool idle = true, busy = false, ready = true;
    unsigned performs = 0, boots = 0, sends = 0;
    uint16_t leds = 0;
    std::string wire;
    AdminSend Send(const uint8_t* d, size_t n) override {
        if(!ready) return AdminSend::NotReady;
        if(busy) return AdminSend::Busy;
        ++sends;
        wire.append(reinterpret_cast<const char*>(d), n);
        return AdminSend::Ok;
    }
    AdminInputs Inputs() override { return in; }
    bool ReaderIdle() override { return idle; }
    ReaderIo LastIo() override { ReaderIo io; io.operation = "WRITE"; io.fault = 4; io.lba = 77; return io; }
    void ApplyLeds(uint16_t m) override { leds = m; }
    void RestartPerformance() override { ++performs; }
    void EnterBootloader() override { ++boots; }
};
struct Rig {
    Card card;
    diag::CardReader reader{card};
    Port port;
    Admin admin{reader, port, "0.5.0", "v6_1"};
    uint32_t now = 1000;
    ddp::FrameParser parser;
    std::vector<std::string> bodies;
    void Host(const char* body) {
        char frame[ddp::kMaxFrame];
        const size_t n = ddp::BuildFrame(body, std::strlen(body), frame, sizeof frame);
        admin.OnUsbReceive(reinterpret_cast<const uint8_t*>(frame), n);
        Step();
    }
    void Step(uint32_t ms = 1) {
        for(uint32_t i = 0; i < ms; ++i) { ++now; admin.Step(now); }
        for(char c : port.wire) if(parser.Feed(c)) bodies.emplace_back(parser.Body());
        port.wire.clear();
    }
    bool Saw(const std::string& prefix) {
        for(const auto& b : bodies) if(b.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
    std::string Last(const std::string& type) {
        for(auto it = bodies.rbegin(); it != bodies.rend(); ++it)
            if(it->compare(0, type.size() + 1, type + " ") == 0) return *it;
        return "";
    }
};

static void Session() {
    Rig r;
    r.port.in.card = true;
    r.Host("READER a=SHARE");
    CHECK(r.Saw("ERR c=READER e=SESSION") && r.reader.Local());  // no session: nothing happens
    r.Host("HELLO proto=2");
    CHECK(r.Saw("ERR c=HELLO e=PROTO") && !r.admin.SessionActive());
    r.bodies.clear();
    r.Host("HELLO proto=1");
    CHECK(r.admin.SessionActive());
    const std::string hello = r.Last("HELLO");
    CHECK(hello.find("app=LooperOnDaisy") != std::string::npos && hello.find("fw=0.5.0") != std::string::npos);
    CHECK(hello.find(",sdmsc") != std::string::npos && hello.find("loopmgmt") != std::string::npos);
    CHECK(hello.find("audio") == std::string::npos && hello.find("jack") == std::string::npos);
    CHECK(r.Saw("ST n=0 ") && r.Saw("SYS ") && r.Saw("READER state=LOCAL") && r.Saw("LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=1 a=STATUS"));
    const std::string st = r.Last("ST");
    CHECK(st.find(" sd=1 ") != std::string::npos && st.find(" lm=MASK ") != std::string::npos && st.size() <= ddp::kMaxBody);
    r.Host("LOOPER a=STATUS");
    CHECK(r.Last("LOOPER") == "LOOPER mode=SDMGMT fw=0.5.0 card=1 ret=1 a=STATUS");
    r.Host("LOOPER a=FROB");
    CHECK(r.Saw("ERR c=LOOPER e=ARG"));
    r.Host("NEB a=STATUS");
    CHECK(r.Saw("ERR c=NEB e=UNKNOWN"));  // Nebulae's message is not understood here
    const std::string reader = r.Last("READER");
    CHECK(reader.find("io=WRITE fault=4 he=0 lba=77") != std::string::npos);
    r.Host("PING n=42");
    CHECK(r.Saw("PONG n=42"));
    r.Host("LED m=CHASE");
    CHECK(r.Saw("ERR c=LED e=UNSUPPORTED"));
    r.Host("FROB");
    CHECK(r.Saw("ERR c=FROB e=UNKNOWN"));
    r.Host("RATE hz=50");
    CHECK(r.Saw("ACK c=RATE hz=50"));
    r.Host("RATE hz=500");
    CHECK(r.Saw("ERR c=RATE e=ARG"));
    // Streaming: ST at the configured rate, SYS/READER/LOOPER once per second.
    r.bodies.clear();
    r.Step(1000);
    unsigned st_count = 0, reader_count = 0;
    for(const auto& b : r.bodies) { st_count += b.rfind("ST ", 0) == 0; reader_count += b.rfind("READER ", 0) == 0; }
    CHECK(st_count >= 45 && st_count <= 55 && reader_count == 1);
    // Host timeout (no frame for 3 s) ends streaming; PINGs keep it alive.
    for(int i = 0; i < 10; ++i) { r.Step(400); r.Host("PING n=1"); }
    CHECK(r.admin.SessionActive());
    r.Step(3100);
    CHECK(!r.admin.SessionActive());
    r.bodies.clear();
    r.Step(2000);
    CHECK(!r.Saw("ST "));
    // Tx busy/not-ready accounting.
    r.Host("HELLO proto=1");
    r.port.ready = false;
    r.Step(200);
    CHECK(r.admin.TxDropped() > 0);
    r.port.ready = true;
}

static void Ownership() {
    Rig r;
    r.Host("HELLO proto=1");
    r.port.in.card = false;
    r.Host("READER a=SHARE");
    CHECK(r.Saw("ERR c=READER e=STATE") && r.reader.Local());  // no card
    r.port.in.card = true;
    r.port.idle = false;
    r.bodies.clear();
    r.Host("READER a=SHARE");
    CHECK(r.Saw("ERR c=READER e=STATE") && r.reader.Local());  // BOT not idle
    r.port.idle = true;
    r.Host("READER a=SHARE");
    CHECK(r.reader.Ready() && r.Saw("READER state=SHARED sectors=1000 busy=0 a=SHARE"));
    // While shared: no return to performance, no bootloader, no RETURN.
    r.bodies.clear();
    r.Host("LOOPER a=PERFORM");
    r.Host("BOOT");
    r.Host("READER a=RETURN");
    CHECK(r.Saw("ERR c=LOOPER e=STATE") && r.Saw("ERR c=BOOT e=BUSY") && r.Saw("ERR c=READER e=STATE"));
    CHECK(r.Last("LOOPER").find("ret=0") != std::string::npos || r.Last("LOOPER").empty());
    // BYE and a host timeout never return the card.
    r.Host("BYE");
    CHECK(r.Saw("ACK c=BYE") && !r.admin.SessionActive() && r.reader.Ready());
    r.Step(5000);
    CHECK(r.reader.Ready());
    // S4 held 2 s does nothing while shared.
    r.port.in.buttons = 1u << 3;
    r.Step(3000);
    r.port.in.buttons = 0;
    r.Step(10);
    CHECK(r.reader.Ready() && r.port.performs == 0);
    // Windows eject -> EJECTED; still no PERFORM before RETURN.
    r.Host("HELLO proto=1");
    CHECK(r.reader.Eject() && r.reader.State() == diag::ReaderState::Ejected);
    r.bodies.clear();
    r.Host("LOOPER a=PERFORM");
    CHECK(r.Saw("ERR c=LOOPER e=STATE"));
    r.Host("READER a=RETURN");
    CHECK(r.reader.Local() && r.Saw("READER state=LOCAL") && r.card.closes == 1);
    r.bodies.clear();
    r.Host("LOOPER a=PERFORM");
    CHECK(r.Saw("LOOPER mode=SDMGMT") && r.Last("LOOPER").find("a=PERFORM") != std::string::npos);
    CHECK(r.port.performs == 0);  // the ACK leaves first
    r.Step(25);
    CHECK(r.port.performs == 1);
    r.Host("LOOPER a=PERFORM");   // a second request after the restart call (fake) is fine
    r.Step(25);
    CHECK(r.port.performs == 2 && r.port.boots == 0);
    // A queued ACK that cannot leave waits up to kFlushMs.
    r.port.busy = true;
    r.Host("BOOT");
    r.Step(30);
    CHECK(r.port.boots == 0);
    r.Step(30);
    CHECK(r.port.boots == 1);
    r.port.busy = false;
}

static void Interruption() {
    Rig r;
    r.port.in.card = true;
    r.Host("HELLO proto=1");
    r.Host("READER a=SHARE");
    r.reader.Interrupt();  // e.g. USB bus reset or card removal during a transfer
    CHECK(r.reader.State() == diag::ReaderState::Interrupted);
    r.port.in.buttons = 1u << 3;
    r.Step(2500);
    r.port.in.buttons = 0;
    CHECK(r.port.performs == 0 && r.reader.State() == diag::ReaderState::Interrupted);  // quarantined
    r.Host("READER a=SHARE");
    CHECK(r.Saw("ERR c=READER e=STATE"));  // no automatic or repeated reshare
    r.Host("READER a=RETURN");             // explicit acknowledgement
    CHECK(r.reader.Local());
    r.Host("LOOPER a=PERFORM");
    r.Step(25);
    CHECK(r.port.performs == 1);
    // Failed open -> ERROR, then acknowledgement.
    Rig e;
    e.port.in.card = true;
    e.card.open_ok = false;
    e.Host("HELLO proto=1");
    e.Host("READER a=SHARE");
    CHECK(e.reader.State() == diag::ReaderState::Error && e.Saw("ERR c=READER e=STATE"));
    e.Host("READER a=RETURN");
    CHECK(e.reader.Local());
}

static void KeysAndLeds() {
    Rig r;
    r.port.in.card = true;
    r.Step(10);
    CHECK((r.port.leds & 0x7FFF) == 0x0001);  // LOCAL: D1
    // S4 together with another key does not count; S4 alone for 2 s returns.
    r.port.in.buttons = (1u << 3) | 1u;
    r.Step(2500);
    CHECK(r.port.performs == 0);
    r.port.in.buttons = 1u << 3;
    r.Step(1990);
    CHECK(r.port.performs == 0);
    r.Step(40);
    CHECK(r.port.performs == 1);
    r.port.in.buttons = 0;
    r.Step(10);
    // Without a PC: EJECTED + S4 returns the card and restarts.
    CHECK(r.reader.Share(true));
    r.Step(10);
    CHECK((r.port.leds & 0x7FFF) == 0x0002);  // SHARED: D2
    CHECK(r.reader.Eject());
    r.Step(10);
    CHECK((r.port.leds & 0x7FFF) == 0x0008);  // EJECTED: D4
    r.port.in.buttons = 1u << 3;
    r.Step(2100);
    CHECK(r.reader.Local() && r.port.performs == 2);
    r.port.in.buttons = 0;
    CHECK(r.reader.Share(true));
    r.reader.Interrupt();
    bool on = false, off = false;
    for(int i = 0; i < 600; ++i) { r.Step(1); on |= (r.port.leds & 0x10) != 0; off |= (r.port.leds & 0x10) == 0; }
    CHECK(on && off);  // INTERRUPTED: D5 blinks
    // D16 shows the SD management personality.
    bool d16 = false;
    for(int i = 0; i < 1200; ++i) { r.Step(1); d16 |= (r.port.leds & 0x8000) != 0; }
    CHECK(d16);
    // Press counters in ST (S1 was pressed once in the S4+S1 chord, S4 twice).
    r.Host("HELLO proto=1");
    r.port.in.buttons = 1; r.Step(5); r.port.in.buttons = 0; r.Step(5);
    r.port.in.buttons = 1; r.Step(5); r.port.in.buttons = 0; r.Step(5);
    r.bodies.clear();
    r.Host("GET");
    CHECK(r.Last("ST").find(" pc=03000002000000") != std::string::npos);
}

int main() {
    Session();
    Ownership();
    Interruption();
    KeysAndLeds();
    std::printf("PASS looper SD management (admin): %u checks; session, ownership, eject/return/perform, interruption quarantine, keys, LEDs\n", checks);
}
