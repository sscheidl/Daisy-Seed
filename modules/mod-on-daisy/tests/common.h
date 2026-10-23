#pragma once
// Shared helpers for the Mod on Daisy host tests.
#include "mod_on_daisy/module.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace test {
using namespace mod;

inline InstrumentInfo MakeInfo() {
    InstrumentInfo info;
    info.key = "test";
    info.version = "0.0.1";
    info.targets[0] = {"Pitch", "+-12 st", 0.25f, true, false};
    info.targets[1] = {"Level", "100%", 1.0f, true, false};
    info.targets[2] = {"Bright", "100%", 1.0f, true, false};
    info.targets[3] = {"Excite", "100%", 1.0f, false, true};
    info.targets[4] = {"Reverb", "100%", 1.0f, false, false};
    info.targets[5] = {"Dry/Wet", "100%", 1.0f, false, false};
    return info; // columns 6 and 7 do not exist
}

// Records sent frames without allocating (usable inside the no-allocation test).
struct Recorder : MidiSender {
    static constexpr std::size_t kFrames = 8;
    uint8_t frames[kFrames][proto::kMaxFrame]{};
    std::size_t sizes[kFrames]{};
    std::size_t count = 0;
    bool connected = true;
    bool Send(const uint8_t* bytes, std::size_t size) override {
        if(!connected)
            return false;
        if(count < kFrames) {
            std::memcpy(frames[count], bytes, size);
            sizes[count] = size;
        }
        ++count;
        return true;
    }
};

struct Reply {
    bool present = false;
    std::size_t size = 0;
    proto::Frame frame;
    std::vector<uint8_t> payload;
};

inline Reply Decode(const uint8_t* bytes, std::size_t size) {
    Reply r;
    r.present = true;
    r.size = size;
    assert(size >= proto::kHeader + 3 && bytes[0] == 0xF0 && bytes[size - 1] == 0xF7);
    assert(proto::ParseHeader(bytes + 1, size - 2, r.frame) == proto::Parse::Ok);
    assert(proto::Verify(bytes + 1, size - 2, r.frame) == 0);
    r.payload.assign(r.frame.payload, r.frame.payload + r.frame.length);
    return r;
}

struct Device {
    InstrumentInfo info = MakeInfo();
    Module module;
    Recorder out;
    uint32_t now = 0;
    uint8_t seq = 0;

    explicit Device(std::size_t block = 48, Diagnostics* diagnostics = nullptr) {
        module.Init(info, 48000.0f, block, 0, diagnostics);
    }
    void SendRaw(const uint8_t* frame, std::size_t size) {
        MidiMessage m;
        m.status = 0xF0;
        m.sysex = frame + 1;
        m.sysex_size = size - 2;
        assert(module.controller.HandleMidi(m));
    }
    // Sends one request and returns the reply produced by the next Service().
    Reply Request(uint8_t command, const std::vector<uint8_t>& payload, uint8_t device = 0) {
        uint8_t frame[proto::kMaxFrame];
        seq = uint8_t((seq + 1) & 127);
        const std::size_t size = proto::BuildFrame(frame, device, command, seq, payload.data(), payload.size());
        assert(size);
        SendRaw(frame, size);
        return Collect();
    }
    Reply Collect() {
        out.count = 0;
        module.controller.Service(++now, out);
        if(!out.count)
            return {};
        return Decode(out.frames[0], out.sizes[0]);
    }
    void Midi(uint8_t status, uint8_t d0, uint8_t d1 = 0) {
        MidiMessage m;
        m.status = status;
        m.data0 = d0;
        m.data1 = d1;
        module.controller.HandleMidi(m);
    }
    void Ticks(std::size_t count, std::size_t samples = 48) {
        for(std::size_t i = 0; i < count; ++i)
            module.engine.Tick(samples);
    }
};

inline std::vector<uint8_t> Bytes(std::initializer_list<unsigned> values) {
    std::vector<uint8_t> v;
    for(unsigned x : values)
        v.push_back(uint8_t(x));
    return v;
}

inline Patch WithCell(Patch p, Source s, std::size_t t, int depth) {
    p.depth[s][t] = int8_t(depth);
    p.SetEnabled(s, t, true);
    return p;
}
}
