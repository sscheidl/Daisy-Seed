// Nebulae Mod on Daisy glue without the firmware: the column table, the USB tap next to
// the other parsers, the tick clock and the clamped offset against the module's Apply.
#include "mod_bridge.h"
#include "modulation.h"
#include "pcm.h"
#include "mod_on_daisy/module.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

using neb::modbridge::LiveEvent;
using neb::modbridge::MidiTap;
using neb::modbridge::SysExFrame;
using neb::modbridge::TickClock;

static void Feed(MidiTap& tap, std::initializer_list<uint8_t> bytes) {
    for(uint8_t b : bytes) tap.Byte(b);
}
static bool Next(MidiTap& tap, uint8_t status, uint8_t a = 0, uint8_t b = 0) {
    LiveEvent e;
    return tap.Pop(e) && e.status == status && e.data0 == a && e.data1 == b;
}
static bool Empty(MidiTap& tap) {
    LiveEvent e;
    SysExFrame f;
    return !tap.Pop(e) && !tap.Pop(f);
}

static void Columns() {
    const mod::InstrumentInfo info = neb::columns::MakeModInfo();
    CHECK(std::strcmp(info.key, "nebulae") == 0 && std::strcmp(info.version, NEBULAE_VERSION) == 0);
    const char* names[] = {"Start", "Speed", "Size", "Density", "Pitch", "Overlap", "Blend", "Window"};
    for(unsigned t = 0; t < mod::kTargets; ++t) {
        CHECK(std::strcmp(info.targets[t].name, names[t]) == 0);
        CHECK(std::strlen(info.targets[t].name) <= 12 && std::strlen(info.targets[t].span_text) <= 8);
        CHECK(!info.targets[t].per_voice && !info.targets[t].latched);
    }
    CHECK(info.targets[neb::Speed].span == .5f && info.targets[neb::Pitch].span == .5f);
    CHECK(info.targets[neb::Start].span == 1.f && info.targets[neb::Window].span == 1.f);
    CHECK(mod::AllowedCells(info) == ~uint64_t(0));  // global columns: all 64 cells, Poly AT included
}

// The audio callback cannot call Engine::Apply (the timer owns the engine), so the
// engine's Unit(base + offset) must stay the same function.
static void SameAsApply() {
    static mod::Module m;
    const mod::InstrumentInfo info = neb::columns::MakeModInfo();
    m.Init(info, 48000.f, neb::columns::kTickFrames);
    mod::Patch p{};
    p.depth[mod::ModWheel][0] = 100;
    p.SetEnabled(mod::ModWheel, 0, true);
    CHECK(m.controller.LoadPatch(p) == mod::Status::Ok);
    for(unsigned wheel = 0; wheel <= 127; wheel += 11) {
        m.live.ModWheel(uint8_t(wheel));
        for(unsigned i = 0; i < 400; ++i) m.engine.Tick(neb::columns::kTickFrames);
        for(unsigned step = 0; step <= 20; ++step) {
            const float base = float(step) / 20.f;
            const float applied = m.engine.Apply(0, base);
            CHECK(applied == neb::Unit(base + m.engine.Offset(0)));
            CHECK(applied >= 0.f && applied <= 1.f);
        }
    }
    // No active connection: offset +0.0f, every base comes back bit for bit.
    CHECK(m.controller.LoadPatch(mod::Patch{}) == mod::Status::Ok);
    m.engine.Tick(neb::columns::kTickFrames);
    CHECK(m.engine.Offset(0) == 0.f && !std::signbit(m.engine.Offset(0)));
    for(unsigned i = 0; i <= 1000; ++i) {
        const float base = float(i) / 1000.f, out = neb::Unit(base + m.engine.Offset(0));
        CHECK(std::memcmp(&base, &out, sizeof base) == 0);
    }
    CHECK(neb::Unit(.5f + std::numeric_limits<float>::infinity()) == 0.f);  // never a non-finite control
}

static void LiveLayer() {
    static MidiTap tap;
    Feed(tap, {0x93, 60, 100});  CHECK(Next(tap, 0x93, 60, 100));
    Feed(tap, {62, 90});         CHECK(Next(tap, 0x93, 62, 90));  // running status
    Feed(tap, {0xA1, 60, 33});   CHECK(Next(tap, 0xA1, 60, 33));
    Feed(tap, {0xD2, 77, 78});   CHECK(Next(tap, 0xD2, 77)); CHECK(Next(tap, 0xD2, 78));
    Feed(tap, {0xB0, 1, 99});    CHECK(Next(tap, 0xB0, 1, 99));
    Feed(tap, {0xB0, 123, 0});   CHECK(Next(tap, 0xB0, 123, 0));
    Feed(tap, {0xB0, 120, 0});   CHECK(Next(tap, 0xB0, 120, 0));
    // The instrument's own messages never enter the matrix queue.
    Feed(tap, {0xB0, 20, 10, 27, 11, 121, 0, 0xE0, 0, 64, 0xC0, 5});
    CHECK(Empty(tap));
    // Real time passes through, also between the bytes of another message.
    Feed(tap, {0x90, 60, 0xF8, 100}); CHECK(Next(tap, 0xF8)); CHECK(Next(tap, 0x90, 60, 100));
    Feed(tap, {0xFA, 0xFC, 0xFE, 0xFF}); CHECK(Next(tap, 0xFA)); CHECK(Empty(tap));
    // Data bytes without a status are ignored; system common cancels running status.
    Feed(tap, {0xF3, 1, 2, 3});
    CHECK(Empty(tap));
}

static void Frames() {
    static MidiTap tap;
    SysExFrame f;
    // Only MD reaches the tap: BL, NB and foreign SysEx are other parsers' business.
    Feed(tap, {0xF0, 0x7D, 0x42, 0x4C, 1, 1, 0xF7});
    Feed(tap, {0xF0, 0x7D, 0x4E, 0x42, 1, 1, 0xF7});
    Feed(tap, {0xF0, 0x41, 0x4D, 0x44, 1, 0xF7});
    CHECK(Empty(tap));
    Feed(tap, {0xF0, 0x7D, 0x4D, 0x44, 1, 0xF8, 2, 3, 0xF7});  // clock inside the frame
    CHECK(tap.Pop(f) && f.size == 6 && f.body[0] == 0x7D && f.body[3] == 1 && f.body[4] == 2 && f.body[5] == 3);
    CHECK(Next(tap, 0xF8));
    // A status byte aborts the frame and starts its own message.
    Feed(tap, {0xF0, 0x7D, 0x4D, 0x44, 1, 0x90, 60, 100, 0xF7});
    CHECK(!tap.Pop(f));
    CHECK(Next(tap, 0x90, 60, 100));
    // F7 clears running status: following data bytes are no notes.
    Feed(tap, {60, 100});
    CHECK(Empty(tap));
    // The longest valid frame fits, one byte more is dropped whole.
    std::vector<uint8_t> longest(mod::proto::kMaxFrame - 2, 5);
    longest[0] = 0x7D; longest[1] = 0x4D; longest[2] = 0x44;
    tap.Byte(0xF0); for(uint8_t b : longest) tap.Byte(b); tap.Byte(0xF7);
    CHECK(tap.Pop(f) && f.size == longest.size());
    tap.Byte(0xF0); for(uint8_t b : longest) tap.Byte(b); tap.Byte(5); tap.Byte(0xF7);
    CHECK(!tap.Pop(f) && !tap.Overflowed());
    // After the drop the next frame is intact.
    Feed(tap, {0xF0, 0x7D, 0x4D, 0x44, 9, 0xF7});
    CHECK(tap.Pop(f) && f.size == 4 && f.body[3] == 9);
}

static void Overflow() {
    static MidiTap tap;
    for(unsigned i = 0; i < MidiTap::kLiveEvents; ++i) Feed(tap, {0x90, 60, 100});
    CHECK(!tap.Overflowed());
    Feed(tap, {0x80, 60, 0});  // the queue is full
    CHECK(tap.Overflowed() && !tap.Overflowed());  // reported once
    unsigned count = 0;
    LiveEvent e;
    while(tap.Pop(e)) { CHECK(e.status == 0x90); ++count; }
    CHECK(count == MidiTap::kLiveEvents);
    for(unsigned i = 0; i < MidiTap::kFrames; ++i) Feed(tap, {0xF0, 0x7D, 0x4D, 0x44, uint8_t(i), 0xF7});
    CHECK(!tap.Overflowed());
    Feed(tap, {0xF0, 0x7D, 0x4D, 0x44, 99, 0xF7});
    CHECK(tap.Overflowed());
    SysExFrame f;
    for(unsigned i = 0; i < MidiTap::kFrames; ++i) CHECK(tap.Pop(f) && f.body[3] == i);
    CHECK(!tap.Pop(f));
}

static void Clock() {
    TickClock c;
    c.Start(1000);
    CHECK(c.Due(1000) == 0);  // second interrupt in the same millisecond: nothing owed
    CHECK(c.Due(1001) == 1);
    CHECK(c.Due(1006) == 5);  // the audio callback held the timer off for a block
    uint32_t total = 0, now = 2000;
    c.Start(now);
    for(unsigned i = 0; i < 1000; ++i) { now += 1 + i % 7; total += c.Due(now); }
    CHECK(total == now - 2000);  // merged interrupts lose no time
    CHECK(c.Dropped() == 0);
    c.Start(0);
    CHECK(c.Due(100) == TickClock::kMaxCatchUp && c.Dropped() == 100 - TickClock::kMaxCatchUp);
    c.Start(0xFFFFFFF0u);
    CHECK(c.Due(4) == TickClock::kMaxCatchUp);  // 20 ms across the 32-bit wrap
    CHECK(c.Dropped() == 100 - TickClock::kMaxCatchUp + 4);
}

int main() {
    Columns();
    SameAsApply();
    LiveLayer();
    Frames();
    Overflow();
    Clock();
    std::printf("PASS Mod on Daisy bridge: %u checks; column table, Apply equivalence, tap, overflow, tick clock\n", checks);
}
