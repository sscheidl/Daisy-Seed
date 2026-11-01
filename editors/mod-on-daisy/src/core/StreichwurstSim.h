#pragma once
// Simulated Streichwurst device: one port that speaks SQ 1.1 (the firmware's own SQ device,
// the generated Streichwurst parameter tables and register) and Mod on Daisy (the
// firmware's own controller with the Streichwurst columns), like the firmware on J7.
// Parameter changes apply at once (no audio). Used by the core tests, the JUCE self-test,
// the "Streichwurst (Sim)" port with --simulators and tools/sq_sim (browser tests).
#include "mod_on_daisy/module.h"
#include "mod_targets.h"
#include "register.h"
#include "step_seq/device.h"
#include <functional>
#include <vector>

namespace editor {

class StreichwurstSim : private sq::Host {
  public:
    using Out = std::function<void(const uint8_t*, std::size_t)>;
    static constexpr const char* kVersion = "0.1.0";

    StreichwurstSim() : device_(*this), info_(streichwurst::product::ModInfo(kVersion)) {
        using namespace streichwurst::product;
        const uint8_t kinds[kComponents] = {sq::KindInstrument, sq::KindEffect, sq::KindProduct};
        const char* ids[kComponents] = {"streichwurst-instrument", "streichwurst-fx", "streichwurst"};
        for(uint8_t k = 0; k < kComponents; ++k) {
            for(uint8_t i = 0; i < Table(k).count; ++i)
                ReadParam(state_, k, Table(k).params[i].id, values_[k][i]);
            components_[k] = {ids[k], kinds[k], kVersion, &Table(k), values_[k]};
        }
        device_.product = "streichwurst";
        device_.version = kVersion;
        device_.protocol_minor = 1;
        device_.boot_id = 0x5EED0002u;
        device_.uid[0] = 0x00410032u;
        device_.uid[1] = 0x3138510Bu;
        device_.uid[2] = 0x32313533u;
        device_.capabilities = sq::CapVoice | sq::CapDin | sq::CapMonitor | sq::CapModMatrix;
        device_.components = components_;
        device_.component_count = kComponents;
        module_.Init(info_, 48000.f, 48);
        module_.EnableExtensions({true, true, false});
    }
    StreichwurstSim(const StreichwurstSim&) = delete;
    StreichwurstSim& operator=(const StreichwurstSim&) = delete;

    // Editor -> device: SQ frames to the SQ device, everything else to Mod on Daisy.
    void Receive(const uint8_t* bytes, std::size_t size, uint32_t now_ms) {
        if(size >= 4 && bytes[0] == 0xF0 && bytes[1] == 0x7D && bytes[2] == 0x53 && bytes[3] == 0x51) {
            device_.Handle(bytes, size, now_ms);
            return;
        }
        mod::MidiMessage m;
        if(size >= 2 && bytes[0] == 0xF0) {
            m.status = 0xF0;
            m.sysex = bytes + 1;
            m.sysex_size = size - 2;
        } else if(size >= 3) {
            m.status = bytes[0];
            m.data0 = bytes[1];
            m.data1 = bytes[2];
        } else {
            return;
        }
        module_.controller.HandleMidi(m);
    }
    // One millisecond: matrix tick, Mod on Daisy service, SQ monitor.
    void Advance(uint32_t now_ms) {
        using namespace streichwurst::product;
        for(int t = 0; t < kMatrixTargets; ++t)
            module_.engine.ReportBase(std::size_t(t), Base(t));
        module_.engine.Tick(48);
        module_.controller.Service(now_ms, sender_);
        device_.Service(now_ms);
    }
    void SetOut(Out out) {
        out_ = out;
        sender_.out = std::move(out);
    }

    // For tests: a pot moves on the kit (the monitor reports it).
    void TurnKnob(uint8_t component, uint16_t id, int32_t value) {
        streichwurst::product::WriteParam(state_, component, id, value, true);
        Sync(component, id);
        device_.ParamChanged(component, id);
    }
    int32_t Value(uint8_t component, uint16_t id) const {
        int32_t v = 0;
        streichwurst::product::ReadParam(state_, component, id, v);
        return v;
    }
    float MatrixOffset(int target) const { return module_.engine.Offset(std::size_t(target)); }
    const mod::Controller& Matrix() const { return module_.controller; }
    sq::Device& Device() { return device_; }
    unsigned Writes() const { return writes_; }
    void Restart() { device_.boot_id ^= 0x00010000u; }

  private:
    struct Sender : mod::MidiSender {
        Out out;
        bool Send(const uint8_t* bytes, std::size_t size) override {
            if(out)
                out(bytes, size);
            return true;
        }
    };
    float Base(int t) const {
        using namespace streichwurst::product;
        auto norm = [this](uint8_t c, uint16_t pid) {
            int32_t v = 0;
            ReadParam(state_, c, pid, v);
            return Normalized(*sq::FindParam(Table(c), pid), v);
        };
        switch(t) {
            case TargetRegistration: return norm(kInstrument, id::Registration);
            case TargetAnimate: return norm(kFx, id::Animate);
            case TargetPhaser: return norm(kFx, id::Phaser);
            case TargetReverb: return norm(kFx, id::Reverb);
            case TargetBalance: return norm(kProduct, id::Balance);
            case TargetSoloTone: return norm(kInstrument, id::SoloTone);
            case TargetTremolo: return norm(kInstrument, id::SoloTremDepth);
            default: return 0.5f;
        }
    }
    void Sync(uint8_t component, uint16_t id) {
        const sq::ComponentParams& t = streichwurst::product::Table(component);
        for(uint8_t i = 0; i < t.count; ++i)
            if(t.params[i].id == id)
                streichwurst::product::ReadParam(state_, component, id, values_[component][i]);
    }
    bool PushParam(uint8_t component, uint16_t id, int32_t value, uint16_t revision) override {
        if(!streichwurst::product::WriteParam(state_, component, id, value))
            return false;
        Sync(component, id);
        device_.view.applied_revision = revision;
        ++writes_;
        return true;
    }
    bool PushPattern(const step_seq::Pattern&, uint16_t) override { return false; }
    bool PushStep(uint8_t, const step_seq::Step&, uint16_t) override { return false; }
    bool PushTransport(uint8_t) override { return false; }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override { return false; }
    bool PushCvEnable(bool) override { return false; }
    void Send(const uint8_t* bytes, std::size_t size) override {
        if(out_)
            out_(bytes, size);
    }

    sq::Device device_;
    streichwurst::product::State state_;
    int32_t values_[streichwurst::product::kComponents][32] = {};
    sq::Component components_[streichwurst::product::kComponents];
    mod::InstrumentInfo info_;
    mod::Module module_;
    Sender sender_;
    Out out_;
    unsigned writes_ = 0;
};

} // namespace editor
