#pragma once
// Simulated SQ device for the Acid Seq panel: the firmware's own SQ device side
// (sequencers/step-seq/include/step_seq/device.h) with a host that applies changes at
// once instead of handing them to an audio callback. Products: "acid-seq" (all four
// components), "step-seq" or an effect alone. Used by the core tests, the JUCE self-test,
// the "Acid Seq (Sim)" port and tools/sq_sim.cpp (browser tests). Opt-in like the MD
// simulators. No timing: Advance() plays the sequencer's step position for the monitor.
#include "step_seq/device.h"
#include "step_seq/param_table.h"
#include <functional>
#include <string>
#include <vector>

namespace editor {

// GCC 13 (x86-64, -O2) reports a false -Wstringop-overflow for the vectorized demo pattern
// stores in the constructor (offsets 5, 10, 15 "into Step::gate"); clang and ARM GCC do not.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
class SqSimulator : private sq::Host {
  public:
    using Out = std::function<void(const uint8_t*, std::size_t)>;

    explicit SqSimulator(const std::string& product = "acid-seq") : device_(*this) {
        const sq::ComponentParams* tables[] = {&sq::kStepSeqComponent, &sq::kAcid303Component, &sq::kDriveComponent,
                                               &sq::kStereoDelayComponent};
        const uint8_t kinds[] = {sq::KindSequencer, sq::KindInstrument, sq::KindEffect, sq::KindEffect};
        for(int i = 0; i < 4; ++i) {
            const bool wanted = product == "acid-seq" || (product == "step-seq" && i == 0) ||
                                (product == "acid-303" && i == 1) || (product == "drive-fx" && i == 2) ||
                                (product == "delay-fx" && i == 3);
            if(!wanted)
                continue;
            values_.emplace_back(tables[i]->count, 0);
            for(uint8_t k = 0; k < tables[i]->count; ++k)
                values_.back()[k] = tables[i]->params[k].def;
            tables_.push_back(tables[i]);
            kinds_.push_back(kinds[i]);
        }
        components_.resize(tables_.size());
        for(std::size_t i = 0; i < tables_.size(); ++i)
            components_[i] = {tables_[i]->id, kinds_[i], "0.1.0", tables_[i], values_[i].data()};
        product_ = product;
        device_.product = product_.c_str();
        device_.version = "0.1.0";
        device_.boot_id = 0x5EED0001u;
        device_.uid[0] = 0x00410032u;
        device_.uid[1] = 0x3138510Bu;
        device_.uid[2] = 0x32313533u;
        device_.capabilities = tables_.size() && tables_[0] == &sq::kStepSeqComponent
                                   ? uint16_t(sq::CapCv | sq::CapDin | sq::CapPattern | sq::CapMonitor | sq::CapCalibration |
                                              (tables_.size() > 1 ? sq::CapVoice : 0))
                                   : uint16_t(sq::CapDin | sq::CapMonitor);
        device_.components = components_.data();
        device_.component_count = uint8_t(components_.size());
        for(int i = 0; i < step_seq::kSteps; ++i) {
            step_seq::Step s;
            s.note = uint8_t(24 + (i * 5) % 12);
            s.gate = i % 4 != 3;
            device_.view.pattern[std::size_t(i)] = s;
        }
        device_.view.sw17 = 1;
        device_.view.state = 2; // running
    }

    // Editor -> device. Replies come back through `out`.
    void Receive(const uint8_t* bytes, std::size_t size, uint32_t now_ms) { device_.Handle(bytes, size, now_ms); }
    // Time passes: the step position moves (120 BPM sixteenths) and the monitor runs.
    void Advance(uint32_t now_ms) {
        if(device_.view.state == 2 && now_ms - last_step_ >= 125) {
            last_step_ = now_ms;
            device_.view.step = int8_t((device_.view.step + 1) % 16);
            device_.view.gate = device_.view.pattern[std::size_t(device_.view.step)].gate;
        }
        device_.Service(now_ms);
    }
    void SetOut(Out out) { out_ = std::move(out); }

    // For tests: a pot moves on the "kit".
    void TurnKnob(std::size_t component, uint16_t id, int32_t value) {
        const sq::ParamDesc* d = sq::FindParam(*tables_[component], id);
        values_[component][std::size_t(d - tables_[component]->params)] = sq::Constrain(*d, value);
        device_.ParamChanged(uint8_t(component), id);
    }
    int32_t Value(std::size_t component, uint16_t id) const {
        const sq::ParamDesc* d = sq::FindParam(*tables_[component], id);
        return d ? values_[component][std::size_t(d - tables_[component]->params)] : 0;
    }
    sq::Device& Device() { return device_; }
    const step_seq::Pattern& Pattern() const { return device_.view.pattern; }
    bool CvEnabled() const { return device_.view.cv_enabled; }
    unsigned Writes() const { return writes_; }
    void Restart() { device_.boot_id ^= 0x00010000u; }

  private:
    bool PushParam(uint8_t component, uint16_t id, int32_t value, uint16_t revision) override {
        if(component >= tables_.size())
            return false;
        const sq::ParamDesc* d = sq::FindParam(*tables_[component], id);
        values_[component][std::size_t(d - tables_[component]->params)] = value;
        device_.view.applied_revision = revision;
        ++writes_;
        return true;
    }
    bool PushPattern(const step_seq::Pattern& p, uint16_t revision) override {
        device_.view.pattern = p;
        device_.view.applied_revision = revision;
        ++writes_;
        return true;
    }
    bool PushStep(uint8_t index, const step_seq::Step& s, uint16_t revision) override {
        device_.view.pattern[index] = s;
        device_.view.applied_revision = revision;
        ++writes_;
        return true;
    }
    bool PushTransport(uint8_t command) override {
        if(!device_.view.sw17)
            return false;
        if(command && device_.view.state == 3)
            device_.view.state = 2;
        else if(!command && device_.view.state == 2)
            device_.view.state = 3;
        else
            return false;
        ++writes_;
        return true;
    }
    bool PushCal(uint8_t, const step_seq::OutputCal&) override {
        device_.view.cv_enabled = false;
        ++writes_;
        return true;
    }
    bool PushCvEnable(bool on) override {
        device_.view.cv_enabled = on;
        ++writes_;
        return true;
    }
    void Send(const uint8_t* bytes, std::size_t size) override {
        if(out_)
            out_(bytes, size);
    }

    sq::Device device_;
    std::string product_;
    std::vector<const sq::ComponentParams*> tables_;
    std::vector<uint8_t> kinds_;
    std::vector<std::vector<int32_t>> values_;
    std::vector<sq::Component> components_;
    Out out_;
    uint32_t last_step_ = 0;
    unsigned writes_ = 0;
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace editor
