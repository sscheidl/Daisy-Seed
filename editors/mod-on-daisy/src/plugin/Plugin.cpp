// Mod on Daisy Editor as a VST3 plugin for Studio One (GUI.md § 3, D6).
//
// An audio effect that passes audio through unchanged. The plugin itself holds the
// Daisy's MIDI port (Windows MIDI Services shares it with Studio One, E7b of 24.10.2026)
// and keeps the editor alive while its window is closed, so the connection and the
// monitor survive. The patch is stored in the project and sent to the device when the
// project is loaded (now or at the next connection). With an Acid Seq device the song state
// is its .acidpatch; loading a song asks before sending it (device first, D13).
#include "../ui/EditorComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>

namespace {
using editor::ui::EditorComponent;

class Processor : public juce::AudioProcessor {
  public:
    Processor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        // Connect as soon as the plugin is on a track, without opening its window.
        juce::MessageManager::callAsync([safe = juce::WeakReference<Processor>(this)] {
            if(auto* p = safe.get())
                p->Content();
        });
    }
    ~Processor() override { content_.reset(); }

    // ------------------------------------------------------------------ audio: pass-through
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        const auto& out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
               && layouts.getMainInputChannelSet() == out;
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for(int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }

    // ------------------------------------------------------------------ editor
    // The editor component lives as long as the plugin; the window only shows it.
    EditorComponent& Content() {
        JUCE_ASSERT_MESSAGE_THREAD
        if(!content_) {
            content_ = std::make_unique<EditorComponent>();
            // MOD_ON_DAISY_NO_AUTOCONNECT=1: tests with a kit attached must not touch it.
            content_->auto_connect = juce::SystemStats::getEnvironmentVariable("MOD_ON_DAISY_NO_AUTOCONNECT", {}).isEmpty();
            content_->patch_changed = [this](const std::string& text) {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = text;
            };
        }
        std::string pending; // a project state that arrived since the last call
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending = std::exchange(restored_, {});
        }
        if(!pending.empty())
            content_->LoadProjectPatch(pending);
        return *content_;
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ------------------------------------------------------------------ project state
    // The .modpatch text (UTF-8), the same as "Save file" writes.
    void getStateInformation(juce::MemoryBlock& dest) override {
        std::lock_guard<std::mutex> lock(mutex_);
        dest.replaceAll(state_.data(), state_.size());
    }
    void setStateInformation(const void* data, int size) override {
        std::string text(static_cast<const char*>(data), std::size_t(std::max(0, size)));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = text;
            restored_ = text;
        }
        // Applied on the message thread; created there if the window was never opened, so
        // the patch reaches the device with the project and not only when the window opens.
        juce::MessageManager::callAsync([safe = juce::WeakReference<Processor>(this)] {
            if(auto* p = safe.get())
                p->Content();
        });
    }

    // ------------------------------------------------------------------ plugin facts
    const juce::String getName() const override { return "Mod on Daisy"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

  private:
    std::unique_ptr<EditorComponent> content_;
    std::mutex mutex_;
    std::string state_, restored_; // state_: for the host; restored_: not yet applied
    JUCE_DECLARE_WEAK_REFERENCEABLE(Processor)
};

class Window : public juce::AudioProcessorEditor {
  public:
    explicit Window(Processor& p) : AudioProcessorEditor(p), content_(p.Content()) {
        addAndMakeVisible(content_);
        setResizable(true, true);
        setResizeLimits(1100, 860, 4000, 3000);
        setSize(content_.getWidth(), content_.getHeight());
        // The connection is started by the plugin itself; opening the window does not
        // connect, closing it does not disconnect.
    }
    ~Window() override { removeChildComponent(&content_); }
    void resized() override { content_.setBounds(getLocalBounds()); }

  private:
    EditorComponent& content_;
};

juce::AudioProcessorEditor* Processor::createEditor() { return new Window(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
