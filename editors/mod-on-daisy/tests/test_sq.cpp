// Host test of the editor's SQ dispatcher and handshake (ARBEITSAUFTRAG Phase 1, R09)
// against the firmware's own SQ device (sequencers/step-seq/include/step_seq/device.h).
// No JUCE. make -C editors/mod-on-daisy test
#include "../src/core/SqSession.h"
#include "step_seq/device.h"
#include <cstdio>
#include <deque>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

using Frame = std::vector<uint8_t>;

// Editor <-> device link with optional loss of the next n frames in either direction.
struct Link {
    std::deque<Frame> to_device, to_editor;
    std::vector<Frame> editor_sent;
    int drop_to_device = 0, drop_to_editor = 0;
};
struct EditorPort : editor::Transport {
    Link& link;
    explicit EditorPort(Link& l) : link(l) {}
    bool Send(const uint8_t* b, std::size_t n) override {
        link.editor_sent.emplace_back(b, b + n);
        if(link.drop_to_device > 0)
            --link.drop_to_device;
        else
            link.to_device.emplace_back(b, b + n);
        return true;
    }
};
struct DevicePort : sq::Host {
    Link& link;
    explicit DevicePort(Link& l) : link(l) {}
    void Send(const uint8_t* b, std::size_t n) override {
        if(link.drop_to_editor > 0)
            --link.drop_to_editor;
        else
            link.to_editor.emplace_back(b, b + n);
    }
    bool PushParam(uint8_t, uint16_t, int32_t, uint16_t) override { return true; }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return true; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return true; }
    bool PushTransport(uint8_t) override { return true; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return true; }
    bool PushCvEnable(bool) override { return true; }
};

static int32_t values[sq::kStepSeqComponent.count];
static sq::Component components[] = {{"step-seq", sq::KindSequencer, "0.1.0", &sq::kStepSeqComponent, values}};

static void Pump(Link& link, sq::Device& device, editor::SqSession& session, double& now, int ms) {
    for(int i = 0; i < ms; ++i) {
        while(!link.to_device.empty()) {
            Frame f = link.to_device.front();
            link.to_device.pop_front();
            device.Handle(f.data(), f.size(), uint32_t(now));
        }
        while(!link.to_editor.empty()) {
            Frame f = link.to_editor.front();
            link.to_editor.pop_front();
            CHECK(session.OnMessage(f.data(), f.size(), now));
        }
        session.Tick(now);
        now += 1;
    }
}

int main() {
    // Dispatcher: MD, SQ and bootloader frames are told apart; others are unknown.
    const uint8_t md[] = {0xF0, 0x7D, 0x4D, 0x44, 1, 0, 0x41, 0, 0, 0, 0xF7};
    const uint8_t sqf[] = {0xF0, 0x7D, 0x53, 0x51, 1, 0x41, 0, 0, 0, 0, 0xF7};
    const uint8_t bl[] = {0xF0, 0x7D, 0x42, 0x4C, 1, 0x41, 0xF7};
    const uint8_t other[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    CHECK(editor::ClassifySysEx(md, sizeof(md)) == editor::Protocol::ModOnDaisy);
    CHECK(editor::ClassifySysEx(sqf, sizeof(sqf)) == editor::Protocol::Sq);
    CHECK(editor::ClassifySysEx(bl, sizeof(bl)) == editor::Protocol::Bootloader);
    CHECK(editor::ClassifySysEx(other, sizeof(other)) == editor::Protocol::Unknown);

    Link link;
    DevicePort device_port(link);
    EditorPort editor_port(link);
    sq::Device device(device_port);
    device.components = components;
    device.component_count = 1;
    device.version = "0.1.0";
    device.boot_id = 0xA5A5;
    device.uid[0] = 7;
    editor::SqSession session(editor_port);
    double now = 0;
    // An MD frame is not consumed by the SQ session.
    CHECK(!session.OnMessage(md, sizeof(md), now));
    // Handshake: IDENTIFY, then the component list; nothing else is sent (device first, D13).
    session.Connect(now);
    Pump(link, device, session, now, 10);
    CHECK(session.GetPhase() == editor::SqSession::Phase::Ready);
    CHECK(session.Identity().product == "step-seq" && session.Identity().boot_id == 0xA5A5);
    CHECK(session.Identity().uid[0] == 7 && session.HasComponent("step-seq"));
    for(const Frame& f : link.editor_sent)
        CHECK(f[5] == sq::Identify || f[5] == sq::GetComponent);
    // Lost request and lost reply: retried after 200 ms with the same request ID; the device
    // answers the retry from its cache (idempotent).
    link.editor_sent.clear();
    link.drop_to_device = 1;
    session.Connect(now);
    Pump(link, device, session, now, 250);
    CHECK(session.GetPhase() == editor::SqSession::Phase::Ready);
    CHECK(link.editor_sent.size() == 3 && link.editor_sent[0] == link.editor_sent[1]);
    link.drop_to_editor = 1;
    session.Connect(now);
    Pump(link, device, session, now, 250);
    CHECK(session.GetPhase() == editor::SqSession::Phase::Ready);
    CHECK(device.counters[sq::CntDuplicate] >= 1);
    // A device that never answers: failed after three retries, counted as lost.
    link.drop_to_device = 100;
    session.Connect(now);
    Pump(link, device, session, now, 1000);
    CHECK(session.GetPhase() == editor::SqSession::Phase::Failed && session.Lost() == 1);
    link.drop_to_device = 0;
    // Stale replies and broken frames are ignored and counted.
    session.Connect(now);
    Pump(link, device, session, now, 10);
    const unsigned ignored = session.Ignored();
    Frame stale = link.editor_sent.back();
    stale[5] |= sq::kResponse; // looks like a reply, wrong CRC now
    CHECK(session.OnMessage(stale.data(), stale.size(), now));
    CHECK(session.Ignored() == ignored + 1);
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: editor SQ dispatcher and handshake against the firmware device (retries, loss, stale replies)\n");
    return 0;
}
