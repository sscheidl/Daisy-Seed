#pragma once
// A Mod on Daisy device on the PC: the firmware's own Controller and Engine with an
// instrument's column table. Serves as "Simulator" port in the editor and as the
// counterpart of the core tests, which can also lose, corrupt and delay frames.
#include "Session.h"
#include "mod_on_daisy/module.h"
#include <array>
#include <deque>
#include <memory>
#include <vector>

namespace editor {
class Simulator : public Transport {
  public:
    // Fault injection, each a probability 0..1 per message; latency in ms each way.
    double drop_requests = 0, drop_replies = 0, corrupt_replies = 0;
    uint32_t latency_ms = 1;
    // Pot positions the simulated instrument reports as column base values; -1 = none.
    std::array<float, mod::kTargets> bases{-1, -1, -1, -1, -1, -1, -1, -1};

    explicit Simulator(const mod::InstrumentInfo& info, uint32_t seed = 1) : info_(info), rng_(seed | 1) {
        module_->Init(info_, kSampleRate, kBlock);
    }
    Simulator(const Simulator&) = delete; // the controller keeps a pointer to info_
    Simulator& operator=(const Simulator&) = delete;

    bool Send(const uint8_t* bytes, std::size_t size) override {
        if(Chance(drop_requests))
            return true; // lost on the way
        inbox_.push_back({now_ + latency_ms, std::vector<uint8_t>(bytes, bytes + size)});
        return true;
    }

    // Short messages from "outside", e.g. a keyboard: 0x90 note on, 0xB0 1 = mod wheel.
    void Midi(uint8_t status, uint8_t data0, uint8_t data1) {
        mod::MidiMessage m;
        m.status = status;
        m.data0 = data0;
        m.data1 = data1;
        module_->controller.HandleMidi(m);
    }

    // Runs the device for `ms` milliseconds (one 48-sample engine tick and one
    // Service() per ms) and hands every message it sends to `deliver(bytes, size)`.
    template <class Deliver>
    void Advance(uint32_t ms, Deliver&& deliver) {
        for(uint32_t i = 0; i < ms; ++i) {
            ++now_;
            while(!inbox_.empty() && inbox_.front().due <= now_) {
                Receive(inbox_.front().bytes);
                inbox_.pop_front();
            }
            for(std::size_t t = 0; t < mod::kTargets; ++t)
                module_->engine.ReportBase(t, bases[t]);
            module_->engine.Tick(kBlock);
            module_->controller.Service(now_, sender_);
            for(std::vector<uint8_t>& m : sender_.sent) {
                if(Chance(drop_replies))
                    continue;
                if(m.size() > 10 && Chance(corrupt_replies))
                    m[1 + Random() % (m.size() - 2)] ^= uint8_t(1u << (Random() % 7));
                outbox_.push_back({now_ + latency_ms, std::move(m)});
            }
            sender_.sent.clear();
            while(!outbox_.empty() && outbox_.front().due <= now_) {
                const std::vector<uint8_t> m = std::move(outbox_.front().bytes);
                outbox_.pop_front();
                deliver(m.data(), m.size());
            }
        }
    }

    const mod::Controller& Device() const { return module_->controller; }
    const mod::InstrumentInfo& Info() const { return info_; }

  private:
    static constexpr float kSampleRate = 48000.0f;
    static constexpr std::size_t kBlock = 48; // 1 kHz control rate
    struct Timed {
        uint32_t due;
        std::vector<uint8_t> bytes;
    };
    struct Collect : mod::MidiSender {
        std::vector<std::vector<uint8_t>> sent;
        bool Send(const uint8_t* bytes, std::size_t size) override {
            sent.emplace_back(bytes, bytes + size);
            return true;
        }
    };

    void Receive(const std::vector<uint8_t>& m) {
        if(m.size() < 2)
            return;
        mod::MidiMessage msg;
        if(m[0] == 0xF0) {
            msg.status = 0xF0;
            msg.sysex = m.data() + 1;
            msg.sysex_size = m.size() - 2;
        } else {
            msg.status = m[0];
            msg.data0 = m.size() > 1 ? m[1] : 0;
            msg.data1 = m.size() > 2 ? m[2] : 0;
        }
        module_->controller.HandleMidi(msg);
    }

    uint32_t Random() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }
    bool Chance(double p) { return p > 0 && double(Random() % 1000000u) < p * 1e6; }

    mod::InstrumentInfo info_;
    std::unique_ptr<mod::Module> module_ = std::make_unique<mod::Module>();
    Collect sender_;
    std::deque<Timed> inbox_, outbox_;
    uint32_t now_ = 1, rng_;
};
}
