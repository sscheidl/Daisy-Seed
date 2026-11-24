#pragma once
// SPDX-License-Identifier: MIT
// Shared USB/control adapter; DSP and physical pot assignments stay separate.
#include "param_table.h"
#include "version.h"
#include "step_seq/device.h"
#include "step_seq/spsc.h"
#include <cmath>

namespace ambient {
class RateControl : public sq::Host {
  public:
    using Sender = void (*)(const uint8_t*, std::size_t);
    RateControl(const char* product, const char* version, bool matrix, Sender sender)
        : device(*this), sender_(sender) {
        component_ = {"ambient-reverb", sq::KindEffect, AMBIENT_REVERB_VERSION,
                      &sq::kAmbientReverbComponent, &value_};
        device.product = product;
        device.version = version;
        device.protocol_minor = matrix ? 1 : 0;
        device.capabilities = sq::CapMonitor | (matrix ? sq::CapModMatrix : 0);
        device.components = &component_;
        device.component_count = 1;
    }
    // Audio thread only: a moved physical rate knob takes over, then queued edits.
    // Work per block is bounded even while requests arrive continuously.
    void AudioTick(int32_t physical_rate = -1) {
        if(physical_rate >= 0)
            audio_.rate = sq::Constrain(sq::kAmbientReverbParams[0], physical_rate);
        Command command;
        for(unsigned i = 0; i < 4 && commands_.Pop(command); ++i) {
            audio_.rate = command.rate;
            audio_.revision = command.revision;
        }
        snapshots_.Back() = audio_;
        snapshots_.Publish();
    }
    float RateHz() const { return float(audio_.rate) * .001f; } // audio thread only
    // Main loop only. Accepted values stay visible until their audio revision lands.
    void Service(uint32_t now, bool connected, unsigned mode = 0) {
        if(snapshots_.Update()) {
            const auto& state = snapshots_.Front();
            device.view.applied_revision = state.revision;
            if(state.revision == device.AcceptedRevision() && value_ != state.rate) {
                value_ = state.rate;
                device.ParamChanged(0, 1);
            }
        }
        device.view.usb_connected = connected;
        device.view.mode = uint8_t(mode);
        if(connected) device.Service(now);
    }
    bool HandleBody(const uint8_t* body, std::size_t size, uint32_t now) {
        uint8_t frame[sq::kMaxFrame + 16];
        const auto n = sq::FromSysExBody(body, size, frame, sizeof(frame));
        return n && device.Handle(frame, n, now);
    }
    bool PushParam(uint8_t component, uint16_t id, int32_t value, uint16_t revision) override {
        return component == 0 && id == 1 && commands_.Push({value, revision});
    }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override {
        if(sender_) sender_(bytes, size);
    }
    sq::Device device;
  private:
    struct Command { int32_t rate; uint16_t revision; };
    struct Snapshot { int32_t rate = 180; uint16_t revision = 0; } audio_;
    step_seq::SpscQueue<Command, 4> commands_;
    step_seq::TripleBuffer<Snapshot> snapshots_;
    Sender sender_;
    int32_t value_ = 180;
    sq::Component component_{};
};
} // namespace ambient
