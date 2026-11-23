#pragma once
// USB-MIDI to the Daisy through JUCE. Received SysEx is queued on the MIDI thread and taken
// by the GUI thread, so the core never runs on two threads.
//
// Sending runs on an own thread (core/SendWorker.h): JUCE's WinMM output waits up to 5 s per
// SysEx when the device stops completing them, which happens when the kit restarts into SD
// management (SD_PAGE_WORK_ORDER.md § 4) and when its firmware stalls (Nebulae 0.2.1,
// P3 of 2026-11-08). The message thread never waits for that:
//
// * Send() only queues. A message that stays in the driver longer than kSendTimeoutMs, or a
//   driver that refuses a message, makes Lost() true; the editor then closes the port and
//   reports that the device is not answering.
// * JUCE 9 opens and destroys MIDI devices on the message thread only (its endpoint list and
//   connection objects assert it), so the devices are opened here and destroyed here as long
//   as the sender is idle. A sender still stuck in the driver when the port closes is detached;
//   once the driver returns it posts the destruction of the devices to the message thread
//   (or, after SendWorker::BeginShutdown() at application exit, leaks them).
// * The input is stopped first (a flag in JUCE), and received frames go to a shared inbox, so a
//   late input callback never touches a destroyed port.
#include "../core/SendWorker.h"
#include "../core/Session.h"
#include "Theme.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace editor::ui {
class MidiPort : public Transport {
  public:
    static constexpr std::size_t kMaxQueued = 256;
    static constexpr uint32_t kSendTimeoutMs = 2000;
    static constexpr uint32_t kCloseWaitMs = 50;

    static SendWorker::Config DefaultConfig() {
        SendWorker::Config c;
        c.max_queued = kMaxQueued;
        c.send_timeout_ms = kSendTimeoutMs;
        c.close_wait_ms = kCloseWaitMs;
        return c;
    }

    // Opens both directions or neither; `error` says why not. Message thread only.
    static std::unique_ptr<MidiPort> Open(const juce::MidiDeviceInfo& out, const juce::MidiDeviceInfo& in,
                                          juce::String& error) {
        std::unique_ptr<MidiPort> port(new MidiPort());
        auto devices = std::make_shared<Devices>(port->inbox_);
        devices->output = juce::MidiOutput::openDevice(out.identifier);
        if(devices->output)
            devices->input = juce::MidiInput::openDevice(in.identifier, &devices->receiver);
        if(!devices->output || !devices->input) {
            error = U("Cannot open port “") + (devices->output ? in.name : out.name)
                + U("”. Is it held by another application (e.g. Studio One)? Disable "
                    "“Daisy Seed External” there.");
            return nullptr;
        }
        juce::MidiOutput* output = devices->output.get();
        port->input_ = devices->input.get();
        port->output_info_ = out;
        port->input_info_ = in;
        port->worker_ = std::make_unique<SendWorker>(
            [output](const std::vector<uint8_t>& m) { return SendNow(*output, m); }, devices, &DisposeOnMessageThread,
            DefaultConfig());
        port->input_->start();
        return port;
    }

    // Self-test only: a port without devices whose driver is `send` (called on the sender
    // thread, may block); replies arrive through Inject().
    static std::unique_ptr<MidiPort> OpenForTest(SendWorker::SendFn send, SendWorker::Config config) {
        std::unique_ptr<MidiPort> port(new MidiPort());
        port->worker_ = std::make_unique<SendWorker>(std::move(send), nullptr, nullptr, config);
        return port;
    }
    void Inject(const uint8_t* bytes, std::size_t size) { inbox_->Push(std::vector<uint8_t>(bytes, bytes + size)); }

    ~MidiPort() override {
        if(input_)
            input_->stop(); // a flag in JUCE: no callback runs after this returns
        SendWorker::Resources devices = worker_->Close();
        devices.reset(); // the sender ended: destroyed here, on the message thread
    }

    // Queues one message for the sender thread. false: the link is lost or closed.
    bool Send(const uint8_t* bytes, std::size_t size) override { return worker_->Send(bytes, size); }

    std::vector<std::vector<uint8_t>> Take() { return inbox_->Take(); }

    // Messages queued but not yet handed to the driver.
    std::size_t Pending() const { return worker_->Pending(); }
    // Nothing queued and nothing in the driver (the self-test waits for it).
    bool SendIdle() const { return worker_->Pending() == 0 && !worker_->Busy(); }
    // The device stopped taking messages (a send overran kSendTimeoutMs or was refused).
    bool Lost() { return worker_->Lost(); }
    std::string LostReason() const { return worker_->LostReason(); }

    const juce::MidiDeviceInfo& Output() const { return output_info_; }
    const juce::MidiDeviceInfo& Input() const { return input_info_; }

  private:
    struct Inbox {
        std::mutex mutex;
        std::vector<std::vector<uint8_t>> received;
        void Push(std::vector<uint8_t> frame) {
            std::lock_guard<std::mutex> lock(mutex);
            if(received.size() < kMaxQueued)
                received.push_back(std::move(frame));
        }
        std::vector<std::vector<uint8_t>> Take() {
            std::lock_guard<std::mutex> lock(mutex);
            return std::exchange(received, {});
        }
    };
    struct Receiver : juce::MidiInputCallback {
        std::shared_ptr<Inbox> inbox;
        explicit Receiver(std::shared_ptr<Inbox> i) : inbox(std::move(i)) {}
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
            if(!m.isSysEx())
                return; // the editor only talks SysEx; notes and CCs belong to the instrument
            std::vector<uint8_t> frame;
            frame.reserve(size_t(m.getSysExDataSize()) + 2);
            frame.push_back(0xF0);
            frame.insert(frame.end(), m.getSysExData(), m.getSysExData() + m.getSysExDataSize());
            frame.push_back(0xF7);
            inbox->Push(std::move(frame));
        }
    };
    // Declared so that the input goes first, then the output, then its callback.
    struct Devices {
        Receiver receiver;
        std::unique_ptr<juce::MidiOutput> output;
        std::unique_ptr<juce::MidiInput> input;
        explicit Devices(std::shared_ptr<Inbox> inbox) : receiver(std::move(inbox)) {}
    };

    static bool SendNow(juce::MidiOutput& output, const std::vector<uint8_t>& m) {
        if(m.size() >= 2 && m[0] == 0xF0)
            return output.sendMessageNow(juce::MidiMessage::createSysExMessage(m.data() + 1, int(m.size() - 2)));
        return output.sendMessageNow(juce::MidiMessage(m.data(), int(m.size())));
    }

    // Runs on a detached sender thread after its driver call returned: JUCE devices are
    // destroyed on the message thread. Without one (the application is ending) they leak.
    static void DisposeOnMessageThread(SendWorker::Resources devices) {
        auto* holder = new SendWorker::Resources(std::move(devices));
        if(juce::MessageManager::getInstanceWithoutCreating() == nullptr
           || !juce::MessageManager::callAsync([holder] { delete holder; }))
            return; // leaked on purpose: destroying them here would break JUCE's thread rule
    }

    MidiPort() = default;

    std::shared_ptr<Inbox> inbox_ = std::make_shared<Inbox>();
    std::unique_ptr<SendWorker> worker_;
    juce::MidiInput* input_ = nullptr; // owned by the devices the worker holds
    juce::MidiDeviceInfo output_info_, input_info_;
};
}
