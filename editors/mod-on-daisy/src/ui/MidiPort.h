#pragma once
// USB-MIDI to the Daisy through JUCE. Received SysEx is queued on the MIDI thread and taken
// by the GUI thread, so the core never runs on two threads. Sending runs on an own thread
// with a bounded queue: JUCE's WinMM output can wait seconds for a SysEx completion when
// the device vanished (exactly the moment the kit restarts into SD management), and the
// message thread must never wait for that (SD_PAGE_WORK_ORDER.md § 4). The destructor waits a
// bounded time for the sender; a sender still stuck in the driver is detached and finishes
// on its own (it owns the output).
#include "../core/Session.h"
#include "Theme.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace editor::ui {
class MidiPort : public Transport, private juce::MidiInputCallback {
  public:
    static constexpr std::size_t kMaxQueued = 256;
    static constexpr uint32_t kCloseWaitMs = 50;

    // Opens both directions or neither; `error` says why not.
    static std::unique_ptr<MidiPort> Open(const juce::MidiDeviceInfo& out, const juce::MidiDeviceInfo& in,
                                          juce::String& error) {
        std::unique_ptr<MidiPort> port(new MidiPort());
        std::unique_ptr<juce::MidiOutput> output = juce::MidiOutput::openDevice(out.identifier);
        if(output)
            port->input_ = juce::MidiInput::openDevice(in.identifier, port.get());
        if(!output || !port->input_) {
            error = U("Cannot open port “") + (output ? in.name : out.name)
                + U("”. Is it held by another application (e.g. Studio One)? Disable "
                    "“Daisy Seed External” there.");
            return nullptr;
        }
        port->sender_ = std::make_shared<Sender>();
        port->sender_->output = std::move(output);
        std::shared_ptr<Sender> s = port->sender_;
        port->thread_ = std::thread([s] { Sender::Loop(*s); });
        port->output_info_ = out;
        port->input_info_ = in;
        port->input_->start();
        return port;
    }
    ~MidiPort() override {
        if(input_)
            input_->stop();
        if(!sender_)
            return;
        Sender& s = *sender_;
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.stop = true;
            s.cv.notify_all();
            s.cv.wait_for(lock, std::chrono::milliseconds(kCloseWaitMs), [&s] { return s.finished; });
        }
        if(thread_.joinable()) {
            std::lock_guard<std::mutex> lock(s.mutex);
            if(s.finished)
                thread_.join();
            else
                thread_.detach(); // stuck in the driver: it ends on its own and releases the output
        }
    }

    // Queues one message for the sender thread. false: the queue is full (the device
    // stopped taking data) or the sender failed.
    bool Send(const uint8_t* bytes, std::size_t size) override {
        std::lock_guard<std::mutex> lock(sender_->mutex);
        if(sender_->stop || sender_->failed || sender_->queue.size() >= kMaxQueued)
            return false;
        sender_->queue.emplace_back(bytes, bytes + size);
        sender_->cv.notify_all();
        return true;
    }

    std::vector<std::vector<uint8_t>> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(received_, {});
    }

    // Messages queued but not yet handed to the driver.
    std::size_t Pending() const {
        std::lock_guard<std::mutex> lock(sender_->mutex);
        return sender_->queue.size();
    }
    bool SendFailed() const {
        std::lock_guard<std::mutex> lock(sender_->mutex);
        return sender_->failed;
    }

    const juce::MidiDeviceInfo& Output() const { return output_info_; }
    const juce::MidiDeviceInfo& Input() const { return input_info_; }

  private:
    struct Sender {
        std::unique_ptr<juce::MidiOutput> output;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::vector<uint8_t>> queue;
        bool stop = false, failed = false, finished = false;

        static void Loop(Sender& s) {
            while(true) {
                std::vector<uint8_t> m;
                {
                    std::unique_lock<std::mutex> lock(s.mutex);
                    s.cv.wait(lock, [&s] { return s.stop || !s.queue.empty(); });
                    if(s.queue.empty())
                        break; // stop with nothing left
                    m = std::move(s.queue.front());
                    s.queue.pop_front();
                }
                bool ok;
                if(m.size() >= 2 && m[0] == 0xF0)
                    ok = s.output->sendMessageNow(juce::MidiMessage::createSysExMessage(m.data() + 1, int(m.size() - 2)));
                else
                    ok = s.output->sendMessageNow(juce::MidiMessage(m.data(), int(m.size())));
                std::lock_guard<std::mutex> lock(s.mutex);
                if(!ok) {
                    s.failed = true;
                    s.queue.clear();
                }
                if(s.stop)
                    break;
            }
            std::lock_guard<std::mutex> lock(s.mutex);
            s.finished = true;
            s.cv.notify_all();
        }
    };

    MidiPort() = default;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if(!m.isSysEx())
            return; // the editor only talks SysEx; notes and CCs belong to the instrument
        std::vector<uint8_t> frame;
        frame.reserve(size_t(m.getSysExDataSize()) + 2);
        frame.push_back(0xF0);
        frame.insert(frame.end(), m.getSysExData(), m.getSysExData() + m.getSysExDataSize());
        frame.push_back(0xF7);
        std::lock_guard<std::mutex> lock(mutex_);
        if(received_.size() < kMaxQueued)
            received_.push_back(std::move(frame));
    }

    std::shared_ptr<Sender> sender_;
    std::thread thread_;
    std::unique_ptr<juce::MidiInput> input_;
    juce::MidiDeviceInfo output_info_, input_info_;
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> received_;
};
}
