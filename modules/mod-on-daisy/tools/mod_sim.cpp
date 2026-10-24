// Host simulator of a Mod on Daisy device: the real Controller and Engine with an
// instrument's column table, driven over stdin/stdout. Used by the browser
// editor's end-to-end test; no hardware involved. Built with MOD_ON_DAISY_EXTENSIONS=1.
//
//   mod_sim [toy-piano|physical-modeling|bench]
//   stdin:  "S <hex>"  MIDI message (SysEx F0 … F7 or a short message)
//           "T <ms>"   advance time: one engine tick (48 samples) and Service() per ms
//           "D"        dump: patch CRC and the active cells
//   stdout: "R <hex>"  each message the device sends, then "OK" after every command
#include "mod_on_daisy/module.h"
#include "../../../instruments/toy-piano/src/modulation.h"
#include "../../../instruments/physical-modeling/src/modulation.h"
#include "../bench/src/scenarios.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Stdout : mod::MidiSender {
    bool Send(const uint8_t* bytes, std::size_t size) override {
        std::printf("R ");
        for(std::size_t i = 0; i < size; ++i)
            std::printf("%02x", bytes[i]);
        std::printf("\n");
        return true;
    }
};

std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> out;
    for(std::size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return out;
}
}

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "toy-piano";
    mod::InstrumentInfo info;
    if(which == "toy-piano")
        info = toy_piano::MakeModInfo();
    else if(which == "physical-modeling")
        info = physical_modeling::MakeModInfo();
    else if(which == "bench")
        info = bench::MakeInfo();
    else {
        std::fprintf(stderr, "unknown instrument %s\n", which.c_str());
        return 2;
    }
    static mod::Module module;
    module.Init(info, 48000.0f, 48);
    // Like the firmware: only the Physical Modeling MOD build enables the extensions
    // (extended monitor, LFO cross modulation, active model); Toy Piano v0.3 and the bench do not.
    if(which == "physical-modeling")
        module.EnableExtensions({true, true, true});
    // Physical Modeling MOD=1 reports its pot positions (power-on defaults here) as
    // column base values; Toy Piano v0.3 and the bench report none (PROTOCOL.md § 3.3).
    float bases[mod::kTargets] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if(which == "physical-modeling") {
        const float pm[mod::kTargets] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.8f, physical_modeling::kLevelBase, 0.25f};
        for(std::size_t t = 0; t < mod::kTargets; ++t)
            bases[t] = pm[t];
    }
    Stdout out;
    uint32_t now = 1;
    auto advance = [&](unsigned ms) {
        for(unsigned i = 0; i < ms; ++i) {
            for(std::size_t t = 0; t < mod::kTargets; ++t)
                module.engine.ReportBase(t, bases[t]);
            if(which == "physical-modeling") // power-on: bank A, SW18 up = Modal Resonator
                module.engine.ReportMode(0, mod::kModeFromDevice);
            module.engine.Tick(48);
            module.controller.Service(++now, out);
        }
    };
    std::string line;
    while(std::getline(std::cin, line)) {
        if(line.empty())
            continue;
        std::istringstream in(line);
        std::string cmd, arg;
        in >> cmd >> arg;
        if(cmd == "S") {
            const std::vector<uint8_t> m = Hex(arg);
            mod::MidiMessage msg;
            if(!m.empty() && m[0] == 0xF0 && m.size() >= 2) {
                msg.status = 0xF0;
                msg.sysex = m.data() + 1;
                msg.sysex_size = m.size() - 2;
            } else if(!m.empty()) {
                msg.status = m[0];
                msg.data0 = m.size() > 1 ? m[1] : 0;
                msg.data1 = m.size() > 2 ? m[2] : 0;
            }
            module.controller.HandleMidi(msg);
            advance(2); // the reply leaves with the next Service() calls
        } else if(cmd == "T") {
            advance(unsigned(std::stoul(arg.empty() ? "1" : arg)));
        } else if(cmd == "D") {
            const mod::Patch& p = module.controller.CurrentPatch();
            std::printf("CRC %08X ACTIVE", unsigned(module.controller.CurrentCrc()));
            for(std::size_t s = 0; s < mod::kSources; ++s)
                for(std::size_t t = 0; t < mod::kTargets; ++t)
                    if(p.Active(s, t))
                        std::printf(" %zu:%zu:%d", s, t, int(p.depth[s][t]));
            std::printf(" NAME %.16s\n", p.name);
        }
        std::printf("OK\n");
        std::fflush(stdout);
    }
}
