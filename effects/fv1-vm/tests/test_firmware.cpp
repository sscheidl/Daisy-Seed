// SPDX-License-Identifier: MIT
// Host tests for the board-independent firmware parts (src/fv1_firmware.h).
#include "fv1_firmware.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace fv1_fw;
namespace fv1 = seed::fx::fv1;

static int failures = 0;
#define CHECK(cond, msg)                                                                                 \
    do {                                                                                               \
        if(!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++failures; }          \
    } while(0)

struct Rig {
    std::unique_ptr<fv1::Storage<float>> st = std::make_unique<fv1::Storage<float>>();
    fv1::Vm<float> vm;
    explicit Rig(const uint32_t* words, float p0, float p1, float p2) {
        vm.Prepare({32000, 32}, *st);
        vm.LoadProgram(words);
        fv1::Parameters p; p.pot[0] = p0; p.pot[1] = p1; p.pot[2] = p2;
        vm.SetParameters(p);
    }
    // Block processing as in the firmware.
    void Run(const std::vector<float>& l, const std::vector<float>& r, std::vector<float>& ol, std::vector<float>& orr) {
        ol.assign(l.size(), 0.f); orr.assign(l.size(), 0.f);
        for(std::size_t i = 0; i < l.size(); i += 32)
            vm.Process({&l[i], &r[i], &ol[i], &orr[i], 32});
    }
};

static void TestBuiltinThru() {
    ProgramBank bank; bank.Init({});
    CHECK(!bank.Local() && bank.Count() == kBuiltinCount, "built-in bank");
    Rig rig(bank.Words(0), .5f, .5f, .5f);
    Noise nl{1}, nr{2};
    std::vector<float> l(256), r(256), ol, orr;
    for(std::size_t i = 0; i < l.size(); ++i) { l[i] = nl.Next(); r[i] = nr.Next(); }
    rig.Run(l, r, ol, orr);
    bool same = true;
    for(std::size_t i = 0; i < l.size(); ++i) same = same && ol[i] == l[i] && orr[i] == r[i];
    CHECK(same, "thru copies both channels exactly");
}

static void TestBuiltinEcho() {
    ProgramBank bank; bank.Init({});
    const float fb = .5f, wet = .75f;
    Rig rig(bank.Words(1), 0.f, fb, wet);
    const std::size_t n = 32 * 1100; // > 2 x 16383 samples
    std::vector<float> l(n, 0.f), r(n, 0.f), ol, orr;
    l[0] = .5f; // line input = 0.5 * (L + R) = 0.25
    rig.Run(l, r, ol, orr);
    CHECK(ol[0] == .5f && orr[0] == 0.f, "dry path at t = 0");
    const double first = .25 * double(wet), second = .25 * double(fb) * double(wet);
    CHECK(std::fabs(ol[16383] - first) < 1e-4 && std::fabs(orr[16383] - first) < 1e-4, "first echo after 16383 samples");
    CHECK(std::fabs(ol[2 * 16383] - second) < 1e-4, "second echo scaled by the feedback");
    double other = 0;
    for(std::size_t i = 1; i < n; ++i)
        if(i != 16383 && i != 2 * 16383) other = std::fmax(other, std::fabs(ol[i]));
    CHECK(other < 1e-6, "no other output");
}

static void TestLocalBank() {
    static const uint32_t table[3][fv1::kProgramWords] = {{1u}, {2u}, {3u}};
    ProgramBank bank; bank.Init({table, 3});
    CHECK(bank.Local() && bank.Count() == 3, "local bank count");
    CHECK(bank.Words(1)[0] == 2u && bank.Words(4)[0] == 2u, "local bank index wraps");
    CHECK(StepIndex(0, -1, 240) == 239 && StepIndex(235, 10, 240) == 5 && StepIndex(3, -10, 240) == 233, "step index");
    CHECK(StepIndex(5, 1, 0) == 0, "empty bank");
}

// Groups: Bank Select = group, Program Change = position; out-of-range requests are ignored.
static void TestGroups() {
    static const uint32_t table[7][fv1::kProgramWords] = {{10u}, {11u}, {12u}, {20u}, {21u}, {30u}, {31u}};
    static const uint16_t offset[3] = {0, 3, 5}, size[3] = {3, 2, 2};
    ProgramBank bank; bank.Init({table, 7, offset, size, 3});
    unsigned index = 99, g = 9, p = 9;
    CHECK(bank.Groups() == 3, "three groups");
    CHECK(bank.Resolve(1, 1, index) && index == 4 && bank.Words(index)[0] == 21u, "bank 1 program 1");
    CHECK(bank.Resolve(2, 0, index) && index == 5, "bank 2 program 0");
    CHECK(!bank.Resolve(1, 2, index) && !bank.Resolve(3, 0, index), "out of range ignored");
    bank.Locate(6, g, p);
    CHECK(g == 2 && p == 1, "locate");
    CHECK(bank.StepGroup(4, 1) == 5 && bank.StepGroup(6, 1) == 0 && bank.StepGroup(0, -1) == 5, "step group");
    static const uint16_t bad_offset[2] = {0, 4}, bad_size[2] = {3, 4};
    ProgramBank broken; broken.Init({table, 7, bad_offset, bad_size, 2});
    CHECK(broken.Groups() == 0 && broken.Resolve(0, 9, index) && index == 2, "gapped groups ignored, flat fallback");
    ProgramBank flat; flat.Init({});
    CHECK(flat.Resolve(1, 0, index) && index == 0 && flat.StepGroup(1, 1) == 1, "built-in: flat banks, +10 wraps");
}

static void TestRemote() {
    Remote r;
    CHECK(r.Handle(0, 23, 5).command == Command::None, "other channel ignored");
    CHECK(r.Handle(15, 22, 1).command == Command::None, "high byte latched");
    Action a = r.Handle(15, 23, 111);
    CHECK(a.command == Command::Program && a.value == 239, "program index 239");
    a = r.Handle(15, 26, 127);
    CHECK(a.command == Command::Pot && a.index == 1 && a.value == 127, "POT1");
    CHECK(r.Handle(15, 24, 2).command == Command::None, "invalid input value");
    CHECK(r.Handle(15, 28, 1).command == Command::Bypass, "bypass");
    CHECK(r.Handle(15, 119, 2).command == Command::Telemetry && r.Handle(15, 119, 3).command == Command::None, "telemetry");
    // Standard Program Change on any channel, bank 0 by default.
    a = r.HandleProgramChange(0, 5);
    CHECK(a.command == Command::BankProgram && a.value == 5 && a.index == 0, "program change, bank 0");
    CHECK(r.Handle(3, 32, 1).command == Command::None, "bank LSB latched on any channel");
    a = r.HandleProgramChange(3, 71);
    CHECK(a.value == 71 && a.index == 1, "bank 1 via LSB");
    r.Handle(0, 0, 2);
    CHECK(r.HandleProgramChange(0, 0).index == 2, "MSB wins over LSB");
    r.Handle(0, 0, 0); r.Handle(0, 32, 0);
    a = r.HandleProgramChange(9, 127);
    CHECK(a.value == 127 && a.index == 0, "bank reset");
    CHECK(r.HandleProgramChange(16, 1).command == Command::None && r.HandleProgramChange(0, 128).command == Command::None,
          "invalid program change");
    CHECK(r.Handle(0, 25, 64).command == Command::None, "remote CCs stay on channel 16");
}

static void TestTelemetryPacket() {
    Meter m; m.Init(480000);
    m.Add(240000); m.Add(300000); m.Add(180000);
    const Snapshot s = m.Take(200, kDouble | kNoise);
    CHECK(s.blocks == 3 && s.max_cycles == 300000 && s.overruns == 0, "meter");
    const auto p = Encode(s);
    // Same values as tests/test_probe.py decodes.
    const uint8_t expected[20] = {2, 8, 39, 0, 106, 48, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 72, 1, 9, 64};
    bool ok = true;
    for(unsigned i = 0; i < 20; ++i)
        ok = ok && p[3 * i] == 0xBF && p[3 * i + 1] == 100 + i && p[3 * i + 2] == expected[i];
    CHECK(ok, "packet bytes");
    m.Add(500000);
    CHECK(m.Take(0, 0).overruns == 1, "overrun counted");
    Snapshot big; big.blocks = 1; big.budget = 1; big.max_cycles = 1000000; big.sum_cycles = 1000000;
    const auto q = Encode(big);
    CHECK(q[3 * 4 + 2] == 127 && q[3 * 5 + 2] == 127 && q[3 * 6 + 2] == 127, "peak saturates at 21 bits");
}

static void TestStatusPacket() {
    Status s; s.rate_hz = 16000.f; s.transpose_cents = -1200.4f; s.mix = .7f; s.bypass = true;
    const auto p = EncodeStatus(s);
    // rate 1600 = 64 + 12 * 128; cents 1200 = 48 + 9 * 128; mix 70; bypass 1.
    const uint8_t expected[10] = {1, 64, 12, 48, 9, 70, 1, 0, 0, 64};
    bool ok = true;
    for(unsigned i = 0; i < 10; ++i) ok = ok && p[3 * i] == 0xBF && p[3 * i + 1] == 80 + i && p[3 * i + 2] == expected[i];
    CHECK(ok, "status packet bytes");
    Status extreme; extreme.transpose_cents = 9999.f; extreme.rate_hz = -5.f; extreme.mix = 2.f;
    const auto q = EncodeStatus(extreme);
    const unsigned cents = q[3 * 3 + 2] | q[3 * 4 + 2] << 7;
    CHECK(cents == 12399 && q[3 * 1 + 2] == 0 && q[3 * 2 + 2] == 0 && q[3 * 5 + 2] == 127, "status values clamp to their fields");
}

int main() {
    TestBuiltinThru();
    TestBuiltinEcho();
    TestLocalBank();
    TestGroups();
    TestRemote();
    TestTelemetryPacket();
    TestStatusPacket();
    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("fv1-vm firmware host tests: all passed\n");
    return 0;
}
