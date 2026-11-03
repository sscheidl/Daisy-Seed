// SPDX-License-Identifier: MIT
#include "../src/core/SqSession.h"
#include "ambient_sim.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

struct AmbientLink : editor::Transport {
    AmbientControlSim sim;
    std::deque<std::vector<uint8_t>> to_device, to_editor;
    unsigned writes = 0;
    AmbientLink() {
        sim.SetOut([this](const uint8_t* b, std::size_t n) { to_editor.emplace_back(b, b + n); });
    }
    bool Send(const uint8_t* b, std::size_t n) override {
        if(n > 5 && b[5] == sq::SetParam) ++writes;
        to_device.emplace_back(b, b + n);
        return true;
    }
    void Pump(editor::SqSession& session, double& now, unsigned count = 100) {
        for(unsigned i = 0; i < count; ++i, ++now) {
            while(!to_device.empty()) {
                const auto f = to_device.front(); to_device.pop_front();
                sim.Receive(f.data(), f.size(), uint32_t(now));
            }
            sim.Advance(uint32_t(now));
            while(!to_editor.empty()) {
                const auto f = to_editor.front(); to_editor.pop_front();
                session.OnMessage(f.data(), f.size(), now);
            }
            session.Tick(now);
        }
    }
};

int main() {
    AmbientLink link;
    editor::SqSession session(link);
    double now = 0;
    session.Connect(now); link.Pump(session, now);
    assert(session.Ready() && link.writes == 0);
    assert(session.HasComponent("ambient-reverb"));
    const auto rate = [&]() { return session.Components()[0].Find("reverb_lfo_rate")->Value(); };
    assert(rate() == .18);
    for(double hz : {1.5, .03, .72}) {
        session.SetParam(0, "reverb_lfo_rate", hz); link.Pump(session, now);
        assert(std::abs(rate() - hz) < .0001);
        assert(std::abs(link.sim.control.RateHz() - hz) < .0001);
        assert(link.sim.control.device.AcceptedRevision() == link.sim.control.device.view.applied_revision);
    }
    link.sim.TurnKnob(0, 1, 410); link.Pump(session, now);
    assert(std::abs(rate() - .41) < .0001);
    // Fill the actual RT queue: excess requests are refused, never silently lost.
    for(unsigned i = 0; i < 4; ++i) assert(link.sim.control.PushParam(0, 1, 500, 10));
    assert(!link.sim.control.PushParam(0, 1, 600, 11));
    link.sim.control.AudioTick();
    assert(std::abs(link.sim.control.RateHz() - .5f) < .0001f);
    std::puts("PASS Ambient editor: device-first read, rate endpoints, audio application, monitor and bounded queue");
}
