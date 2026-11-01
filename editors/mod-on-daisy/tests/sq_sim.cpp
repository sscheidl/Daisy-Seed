// Host simulator of an SQ device for the browser tests (web/tests/sq_client.test.cjs,
// web/tests/e2e_sq.cjs): the firmware's SQ device side through SqSimulator, as text lines.
//   sq_sim [product]          product: acid-seq (default), step-seq, acid-303, drive-fx, delay-fx,
//                             streichwurst (SQ 1.1 and Mod on Daisy on the same port)
//   S <hex frame>             frame to the device; replies as "R <hex>"
//   T <ms>                    time passes (monitor frames as "R <hex>")
//   K <component> <id> <milli> a knob on the kit moves
//   Q                         quit
// Every command ends with "OK".
#include "../src/core/SqSimulator.h"
#include "../src/core/StreichwurstSim.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// After a frame: the Streichwurst main loop serves Mod on Daisy at once (one reply per
// millisecond), so its replies follow without a "T" command. Acid Seq is unchanged.
static void Settle(editor::SqSimulator&, uint32_t&) {}
static void Settle(editor::StreichwurstSim& sim, uint32_t& now) {
    for(int i = 0; i < 3; ++i)
        sim.Advance(++now);
}

// Both simulators behind the same commands.
template <class Sim>
static int Run(Sim& sim) {
    sim.SetOut([](const uint8_t* b, std::size_t n) {
        std::string line = "R ";
        char hex[3];
        for(std::size_t i = 0; i < n; ++i) {
            std::snprintf(hex, sizeof(hex), "%02x", b[i]);
            line += hex;
        }
        std::cout << line << "\n";
    });
    uint32_t now = 0;
    std::string line;
    while(std::getline(std::cin, line)) {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if(cmd == "Q")
            break;
        if(cmd == "S") {
            std::string hex;
            in >> hex;
            std::vector<uint8_t> frame;
            for(std::size_t i = 0; i + 1 < hex.size(); i += 2)
                frame.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
            sim.Receive(frame.data(), frame.size(), now);
            Settle(sim, now);
        } else if(cmd == "T") {
            uint32_t ms = 0;
            in >> ms;
            for(uint32_t i = 0; i < ms; ++i)
                sim.Advance(++now);
        } else if(cmd == "K") {
            unsigned component = 0, id = 0;
            long milli = 0;
            in >> component >> id >> milli;
            sim.TurnKnob(uint8_t(component), uint16_t(id), int32_t(milli));
        }
        std::cout << "OK" << std::endl;
    }
    return 0;
}

int main(int argc, char** argv) {
    const std::string product = argc > 1 ? argv[1] : "acid-seq";
    if(product == "streichwurst") {
        static editor::StreichwurstSim sim;
        return Run(sim);
    }
    static editor::SqSimulator sim(product);
    return Run(sim);
}
