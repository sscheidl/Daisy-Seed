// Host simulator of an SQ device for the browser tests (web/tests/sq_client.test.cjs,
// web/tests/e2e_sq.cjs): the firmware's SQ device side through SqSimulator, as text lines.
//   sq_sim [product]          product: acid-seq (default), step-seq, acid-303, drive-fx, delay-fx
//   S <hex frame>             frame to the device; replies as "R <hex>"
//   T <ms>                    time passes (monitor frames as "R <hex>")
//   K <component> <id> <milli> a knob on the kit moves
//   Q                         quit
// Every command ends with "OK".
#include "../src/core/SqSimulator.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    editor::SqSimulator sim(argc > 1 ? argv[1] : "acid-seq");
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
        } else if(cmd == "T") {
            uint32_t ms = 0;
            in >> ms;
            for(uint32_t i = 0; i < ms; ++i)
                sim.Advance(++now);
        } else if(cmd == "K") {
            unsigned component = 0, id = 0;
            long milli = 0;
            in >> component >> id >> milli;
            sim.TurnKnob(component, uint16_t(id), int32_t(milli));
        }
        std::cout << "OK" << std::endl;
    }
    return 0;
}
