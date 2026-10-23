#pragma once
// USB-MIDI to the Daisy through JUCE. Received SysEx is queued on the MIDI thread
// and taken by the GUI thread, so the core never runs on two threads.
#include "../core/Session.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <mutex>
#include <utility>
#include <vector>

namespace editor::ui {
class MidiPort : public Transport, private juce::MidiInputCallback {
  public:
    // Opens both directions or neither; `error` says why not.
    static std::unique_ptr<MidiPort> Open(const juce::MidiDeviceInfo& out, const juce::MidiDeviceInfo& in,
                                          juce::String& error) {
        std::unique_ptr<MidiPort> port(new MidiPort());
        port->output_ = juce::MidiOutput::openDevice(out.identifier);
        if(port->output_)
            port->input_ = juce::MidiInput::openDevice(in.identifier, port.get());
        if(!port->output_ || !port->input_) {
            error = U("Port „") + (port->output_ ? in.name : out.name)
                + U("“ lässt sich nicht öffnen. Ist er von einer anderen Anwendung belegt (z. B. Studio One)? Dort "
                    "„Daisy Seed External“ deaktivieren.");
            return nullptr;
        }
        port->output_info_ = out;
        port->input_info_ = in;
        port->input_->start();
        return port;
    }
    ~MidiPort() override {
        if(input_)
            input_->stop();
    }

    bool Send(const uint8_t* bytes, std::size_t size) override {
        if(size >= 2 && bytes[0] == 0xF0)
            return output_->sendMessageNow(juce::MidiMessage::createSysExMessage(bytes + 1, int(size - 2)));
        return output_->sendMessageNow(juce::MidiMessage(bytes, int(size)));
    }

    std::vector<std::vector<uint8_t>> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(received_, {});
    }

    const juce::MidiDeviceInfo& Output() const { return output_info_; }
    const juce::MidiDeviceInfo& Input() const { return input_info_; }

  private:
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
        if(received_.size() < 256)
            received_.push_back(std::move(frame));
    }

    std::unique_ptr<juce::MidiOutput> output_;
    std::unique_ptr<juce::MidiInput> input_;
    juce::MidiDeviceInfo output_info_, input_info_;
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> received_;
};
}
