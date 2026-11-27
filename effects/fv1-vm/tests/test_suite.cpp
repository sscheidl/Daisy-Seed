// SPDX-License-Identifier: MIT
// Product test for the FV-1 FX Suite (PROGRAMS=otherworld): the firmware program bank with the committed
// Otherworld table, its MIDI bank mapping and panel group stepping, and a short run of every program through
// the same Vm<float> block path as the firmware. Program behaviour is tested in effects/fv1-banks/otherworld.
#include "fv1_firmware.h"
#include "fv1bank/otherworld_programs.h"
#include "fv1bank/otherworld_version.h"
#include "version.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace fv1_fw;
namespace fv1 = seed::fx::fv1;
namespace ow = fv1_bank_otherworld;

static int failures = 0;
#define CHECK(cond, msg)                                                                                 \
    do {                                                                                               \
        if(!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); ++failures; }          \
    } while(0)

int main() {
    ProgramBank bank;
    bank.Init({ow::kPrograms, ow::kCount, ow::kGroupOffset, ow::kGroupSize, ow::kGroupCount});
    CHECK(bank.Local() && bank.Count() == 32 && bank.Groups() == 4, "32 programs in 4 groups");
    unsigned index = 99, g = 9, p = 9;
    CHECK(bank.Resolve(0, 16, index) && index == 16, "bank 0 (Slicer) program 16 = Brownian Dust");
    CHECK(bank.Resolve(1, 1, index) && std::strcmp(ow::kId[index], "172_fog_bank") == 0, "bank 1 program 1 = Fog Bank");
    CHECK(bank.Resolve(3, 6, index) && std::strcmp(ow::kId[index], "211_moonquake_clk") == 0, "bank 3 program 6 = Moonquake");
    CHECK(!bank.Resolve(1, 4, index) && !bank.Resolve(4, 0, index), "beyond a group or the table: ignored");
    bank.Locate(21, g, p);
    CHECK(g == 2 && p == 0, "Avalanche is the first Distortion program");
    CHECK(bank.StepGroup(5, 1) == 17 && bank.StepGroup(31, 1) == 0 && bank.StepGroup(0, -1) == 25, "group keys");
    CHECK(StepIndex(31, 1, bank.Count()) == 0 && StepIndex(0, -1, bank.Count()) == 31, "program keys wrap");
    CHECK(bank.Words(19) == ow::kPrograms[19], "words come straight from the bank table");

    // Every program through the firmware's block path: finite output after a program change (Reset).
    auto st = std::make_unique<fv1::Storage<float>>();
    fv1::Vm<float> vm;
    vm.Prepare({32000, 32}, *st);
    Noise nl{12345}, nr{12346};
    float l[32], r[32], ol[32], orr[32];
    bool finite = true;
    for(unsigned i = 0; i < bank.Count(); ++i) {
        vm.LoadProgram(bank.Words(i));
        vm.Reset();
        for(int b = 0; b < 200; ++b) {
            for(int k = 0; k < 32; ++k) { l[k] = nl.Next(); r[k] = nr.Next(); }
            CHECK(vm.Process({l, r, ol, orr, 32}), "block accepted");
            for(int k = 0; k < 32; ++k) finite = finite && std::isfinite(ol[k]) && std::isfinite(orr[k]);
        }
    }
    CHECK(finite, "finite output for all programs on noise input");
    CHECK(std::strcmp(FV1_FX_SUITE_VERSION, "") != 0 && std::strcmp(SEED_FV1_BANK_OTHERWORLD_VERSION, "") != 0, "versions");
    if(failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("fv1-fx-suite %s host tests (bank %s, fv1-vm %s): all passed\n", FV1_FX_SUITE_VERSION,
                SEED_FV1_BANK_OTHERWORLD_VERSION, SEED_FV1VM_VERSION);
    return 0;
}
