// SPDX-License-Identifier: MIT
// Host tests for the looper's MIDI CC mapping.
#include "midi_control.h"
#include "looper_engine.h"
#include <cstdio>
#include <thread>

using namespace looper::midi;

static int failures = 0;
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void TestButtons() {
    Receiver r;
    CHECK(r.TakePresses() == 0);
    // Every key has its CC; a press is a step from below 64 to 64 or more.
    for (unsigned b = 0; b < kButtons; ++b) {
        CHECK(r.ControlChange(uint8_t(kFirstButtonCc + b), 127));
        CHECK(r.TakePresses() == 1u << b);
        CHECK(r.ControlChange(uint8_t(kFirstButtonCc + b), 0));
        CHECK(r.TakePresses() == 0);
    }
    const uint8_t record = kFirstButtonCc + Record, play = kFirstButtonCc + Play;
    r.ControlChange(record, 64);  // threshold counts as pressed
    r.ControlChange(record, 100);  // still held: no second press
    r.ControlChange(record, 127);
    r.ControlChange(play, 127);
    CHECK(r.TakePresses() == ((1u << Record) | (1u << Play)));
    r.ControlChange(record, 63);  // released
    r.ControlChange(record, 64);
    r.ControlChange(record, 0);
    r.ControlChange(record, 127);  // two presses before the callback: one press bit
    CHECK(r.TakePresses() == (1u << Record));
    CHECK(r.TakePresses() == 0);
    // Values above 127 cannot occur on the wire; the top bit is ignored.
    r.ControlChange(play, 0);
    r.ControlChange(play, 0x80 | 10);
    CHECK(r.TakePresses() == 0);
    std::printf("%s buttons: CC102-113, press on the step to >= 64, held values ignored\n",
                failures ? "FAIL" : "PASS");
}

static void TestKnobs() {
    const int before = failures;
    Receiver r;
    uint8_t v = 0;
    for (unsigned k = 0; k < kKnobs; ++k)
        CHECK(!r.TakeKnob(k, v));
    CHECK(r.ControlChange(kFirstKnobCc + Size, 90));
    CHECK(r.TakeKnob(Size, v) && v == 90 && !r.TakeKnob(Size, v));
    r.ControlChange(kFirstKnobCc + Size, 90);  // the same value again is still news
    CHECK(r.TakeKnob(Size, v) && v == 90);
    r.ControlChange(kFirstKnobCc + Decay, 1);
    r.ControlChange(kFirstKnobCc + Decay, 2);  // only the latest value counts
    CHECK(r.TakeKnob(Decay, v) && v == 2 && !r.TakeKnob(Start, v));
    CHECK(!r.TakeKnob(kKnobs, v));
    // Controllers outside the map are not the looper's.
    for (unsigned c = 0; c < 128; ++c) {
        const bool mapped = (c >= kFirstKnobCc && c < kFirstKnobCc + kKnobs) ||
                            (c >= kFirstButtonCc && c < kFirstButtonCc + kButtons);
        Receiver fresh;
        CHECK(fresh.ControlChange(uint8_t(c), 127) == mapped);
    }
    CHECK(SemitonesFromCc(64) == 0.f && SemitonesFromCc(76) == 12.f && SemitonesFromCc(40) == -24.f);
    CHECK(SemitonesFromCc(0) == looper::kMinSemitones && SemitonesFromCc(100) == 36.f &&
          SemitonesFromCc(127) == looper::kMaxSemitones);
    CHECK(UnitFromCc(0) == 0.f && UnitFromCc(127) == 1.f && UnitFromCc(64) > .5f && UnitFromCc(64) < .51f);
    std::printf("%s knobs: CC20-27 with sequence, latest value, semitone and unit scaling\n",
                failures == before ? "PASS" : "FAIL");
}

static void TestTakeover() {
    const int before = failures;
    Takeover t;
    CHECK(!t.UseMidi(.3f, false));  // without MIDI the pot wins
    CHECK(t.UseMidi(.3f, true));    // a CC takes over
    CHECK(t.UseMidi(.31f, false) && t.UseMidi(.29f, false) && t.UseMidi(.319f, false));
    CHECK(!t.UseMidi(.33f, false));  // moved more than 2 %: the pot is back
    CHECK(!t.UseMidi(.3f, false));
    CHECK(t.UseMidi(.9f, true) && !t.UseMidi(.85f, false));
    std::printf("%s takeover: a CC takes the knob, a 2 %% pot move takes it back\n",
                failures == before ? "PASS" : "FAIL");
}

// One writer, one reader, as main loop and audio callback: every knob value
// seen was sent, the last value and the last press always arrive.
static void TestThreads() {
    const int before = failures;
    Receiver r;
    constexpr unsigned kRounds = 20000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (unsigned i = 0; i < kRounds; ++i) {
            r.ControlChange(kFirstKnobCc + Start, uint8_t(i % 100));
            r.ControlChange(kFirstButtonCc + Erase, 127);
            r.ControlChange(kFirstButtonCc + Erase, 0);
        }
        r.ControlChange(kFirstButtonCc + Seam, 127);  // the last message
        done.store(true);
    });
    bool valid = true, seam = false;
    uint8_t v = 0, last = 0;
    while (!done.load()) {
        seam |= (r.TakePresses() & (1u << Seam)) != 0;  // presses between reads collapse
        if (r.TakeKnob(Start, v)) {
            valid = valid && v < 100;
            last = v;
        }
    }
    writer.join();
    seam |= (r.TakePresses() & (1u << Seam)) != 0;
    if (r.TakeKnob(Start, v))
        last = v;
    CHECK(valid && seam && last == (kRounds - 1) % 100);
    std::printf("%s threads: concurrent writer and reader stay consistent\n",
                failures == before ? "PASS" : "FAIL");
}

int main() {
    TestButtons();
    TestKnobs();
    TestTakeover();
    TestThreads();
    if (failures) {
        std::printf("FAILED: %d MIDI check(s)\n", failures);
        return 1;
    }
    std::printf("All MIDI tests passed\n");
    return 0;
}
