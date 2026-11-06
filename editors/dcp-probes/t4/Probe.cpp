// DCP T4 probe (docs/dcp/T4_PARAMETER_TITLES.md): an instrument with eight automation
// slots whose IDs stay fixed while their display names follow a selectable "firmware"
// mapping. Renaming calls updateHostDisplay(parameterInfoChanged), which JUCE turns into
// the VST3 restartComponent(kParamTitlesChanged). The log shows what the host did.
//
// No Daisy, no MIDI port, no DSP: output is silence. Throw-away test code.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

namespace {
constexpr int kSlots = 8;
constexpr int kMappings = 3;
const char* const kMappingNames[kMappings] = {"Generic", "Firmware A (Toy Piano)", "Firmware B (Streichwurst)"};
const char* const kNames[kMappings][kSlots] = {
    {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8"},
    {"A Cutoff", "A Resonance", "A Drive", "A Env Amount", "A Reverb Mix", "A Reverb Decay", "A Width",
     "A Volume"},
    {"B Crescendo", "B Release", "B Balance", "B Animate", "B Phaser", "B Reverb", "B Master", "B Tremolo"},
};
constexpr int kDeferMs = 5000;

// Fixed ID "slot01" … "slot08"; the name comes from the current mapping.
class SlotParameter : public juce::AudioParameterFloat {
  public:
    SlotParameter(int slot, const std::atomic<int>& mapping)
        : AudioParameterFloat(juce::ParameterID{"slot" + juce::String(slot + 1).paddedLeft('0', 2), 1},
                              kNames[0][slot], juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f),
          slot_(slot), mapping_(mapping) {}
    juce::String getName(int max) const override {
        return juce::String(kNames[mapping_.load()][slot_]).substring(0, max);
    }

  private:
    int slot_;
    const std::atomic<int>& mapping_;
};

class Processor : public juce::AudioProcessor, private juce::Timer {
  public:
    Processor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        for(int i = 0; i < kSlots; ++i) {
            auto p = std::make_unique<SlotParameter>(i, mapping_);
            slots_[std::size_t(i)] = p.get();
            addParameter(p.release());
        }
        Log("plugin created, mapping " + juce::String(kMappingNames[0]));
    }
    ~Processor() override { stopTimer(); }

    // ------------------------------------------------------------------ mapping
    int Mapping() const { return mapping_.load(); }
    bool defer_after_load = false; // persisted; wait kDeferMs before announcing restored names

    void SetMapping(int m, const juce::String& why) {
        mapping_.store(juce::jlimit(0, kMappings - 1, m));
        Log("mapping -> " + juce::String(kMappingNames[mapping_.load()]) + " (" + why + "), names not yet announced");
    }
    void Announce(const juce::String& why) {
        JUCE_ASSERT_MESSAGE_THREAD
        updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
        Log("names announced to host (" + why + "): " + NameList());
    }
    void AnnounceLater(const juce::String& why) {
        pending_why_ = why;
        startTimer(kDeferMs);
        Log("announcement scheduled in " + juce::String(kDeferMs / 1000) + " s (" + why + ")");
    }

    juce::String NameList() const {
        juce::StringArray names;
        for(auto* p : slots_)
            names.add(p->getName(128));
        return names.joinIntoString(", ");
    }
    juce::AudioParameterFloat& Slot(int i) { return *slots_[std::size_t(i)]; }
    int NotesReceived() const { return notes_.load(); }

    // ------------------------------------------------------------------ log
    void Log(const juce::String& line) {
        const juce::ScopedLock lock(log_lock_);
        log_.add(juce::Time::getCurrentTime().formatted("%H:%M:%S ") + line);
    }
    juce::String LogText() const {
        const juce::ScopedLock lock(log_lock_);
        return log_.joinIntoString("\n");
    }
    int LogSize() const {
        const juce::ScopedLock lock(log_lock_);
        return log_.size();
    }

    // ------------------------------------------------------------------ audio
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        const auto& out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        buffer.clear();
        for(const auto event : midi)
            if(event.getMessage().isNoteOn())
                notes_.fetch_add(1, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ state
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::XmlElement xml("DcpT4Probe");
        xml.setAttribute("mapping", Mapping());
        xml.setAttribute("defer", defer_after_load);
        for(auto* p : slots_)
            xml.setAttribute(p->getParameterID(), double(p->get()));
        copyXmlToBinary(xml, dest);
        Log("getStateInformation: mapping " + juce::String(kMappingNames[Mapping()]));
    }
    void setStateInformation(const void* data, int size) override {
        const auto xml = getXmlFromBinary(data, size);
        if(!xml || !xml->hasTagName("DcpT4Probe")) {
            Log("setStateInformation: unreadable state");
            return;
        }
        defer_after_load = xml->getBoolAttribute("defer");
        for(auto* p : slots_)
            *p = float(xml->getDoubleAttribute(p->getParameterID(), 0.5));
        SetMapping(xml->getIntAttribute("mapping"), "setStateInformation");
        Log("plugin names now (host not told yet): " + NameList());
        juce::MessageManager::callAsync([safe = juce::WeakReference<Processor>(this)] {
            if(auto* p = safe.get()) {
                if(p->defer_after_load)
                    p->AnnounceLater("project load, deferred");
                else
                    p->Announce("project load");
            }
        });
    }

    // ------------------------------------------------------------------ plugin facts
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DCP T4 Probe"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

  private:
    void timerCallback() override {
        stopTimer();
        Announce(pending_why_);
    }

    std::atomic<int> mapping_{0};
    std::array<juce::AudioParameterFloat*, kSlots> slots_{};
    std::atomic<int> notes_{0};
    juce::String pending_why_;
    juce::CriticalSection log_lock_;
    juce::StringArray log_;
    JUCE_DECLARE_WEAK_REFERENCEABLE(Processor)
};

class Window : public juce::AudioProcessorEditor, private juce::Timer {
  public:
    explicit Window(Processor& p) : AudioProcessorEditor(p), proc_(p) {
        for(int m = 0; m < kMappings; ++m)
            mapping_.addItem(kMappingNames[m], m + 1);
        mapping_.setSelectedId(p.Mapping() + 1, juce::dontSendNotification);
        addAndMakeVisible(mapping_);

        now_.setButtonText("Rename now");
        now_.onClick = [this] {
            proc_.SetMapping(mapping_.getSelectedId() - 1, "button");
            proc_.Announce("Rename now");
        };
        later_.setButtonText("Rename in 5 s");
        later_.onClick = [this] {
            proc_.SetMapping(mapping_.getSelectedId() - 1, "button");
            proc_.AnnounceLater("Rename in 5 s");
        };
        silent_.setButtonText("Rename silently");
        silent_.onClick = [this] { proc_.SetMapping(mapping_.getSelectedId() - 1, "silent, no announcement"); };
        defer_.setButtonText("After project load, wait 5 s before announcing names");
        defer_.setToggleState(p.defer_after_load, juce::dontSendNotification);
        defer_.onClick = [this] { proc_.defer_after_load = defer_.getToggleState(); };
        copy_.setButtonText("Copy log");
        copy_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(proc_.LogText()); };
        for(auto* c : std::initializer_list<juce::Component*>{&now_, &later_, &silent_, &defer_, &copy_, &notes_})
            addAndMakeVisible(c);

        for(int i = 0; i < kSlots; ++i) {
            auto& s = sliders_[std::size_t(i)];
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
            attachments_[std::size_t(i)] = std::make_unique<juce::SliderParameterAttachment>(proc_.Slot(i), s);
            addAndMakeVisible(s);
            addAndMakeVisible(labels_[std::size_t(i)]);
        }
        log_.setMultiLine(true);
        log_.setReadOnly(true);
        log_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        addAndMakeVisible(log_);
        setResizable(true, true);
        setSize(760, 620);
        startTimerHz(10);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto row = r.removeFromTop(28);
        mapping_.setBounds(row.removeFromLeft(230));
        row.removeFromLeft(8);
        now_.setBounds(row.removeFromLeft(110));
        row.removeFromLeft(6);
        later_.setBounds(row.removeFromLeft(110));
        row.removeFromLeft(6);
        silent_.setBounds(row.removeFromLeft(130));
        row.removeFromLeft(6);
        copy_.setBounds(row.removeFromLeft(90));
        r.removeFromTop(6);
        auto row2 = r.removeFromTop(24);
        defer_.setBounds(row2.removeFromLeft(420));
        notes_.setBounds(row2);
        r.removeFromTop(6);
        for(int i = 0; i < kSlots; ++i) {
            auto line = r.removeFromTop(26);
            labels_[std::size_t(i)].setBounds(line.removeFromLeft(220));
            sliders_[std::size_t(i)].setBounds(line);
        }
        r.removeFromTop(8);
        log_.setBounds(r);
    }

  private:
    void timerCallback() override {
        for(int i = 0; i < kSlots; ++i)
            labels_[std::size_t(i)].setText(proc_.Slot(i).getParameterID() + "  " + proc_.Slot(i).getName(64),
                                             juce::dontSendNotification);
        notes_.setText("MIDI notes received: " + juce::String(proc_.NotesReceived()), juce::dontSendNotification);
        if(const int n = proc_.LogSize(); n != shown_) {
            shown_ = n;
            log_.setText(proc_.LogText(), false);
            log_.moveCaretToEnd();
        }
    }

    Processor& proc_;
    juce::ComboBox mapping_;
    juce::TextButton now_, later_, silent_, copy_;
    juce::ToggleButton defer_;
    juce::Label notes_;
    std::array<juce::Label, kSlots> labels_;
    std::array<juce::Slider, kSlots> sliders_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kSlots> attachments_;
    juce::TextEditor log_;
    int shown_ = -1;
};

juce::AudioProcessorEditor* Processor::createEditor() { return new Window(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
