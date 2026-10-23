#pragma once
// The editor window's content (GUI.md § 2): status bar with the device, the grid, the
// cell editor, the source bar and the file row. Everything runs on the GUI thread;
// a 10 ms timer drives the session (timeouts, coalesced edits) and the simulator.
#include "../core/PatchFile.h"
#include "../core/SimInstruments.h"
#include "../core/Simulator.h"
#include "MatrixGrid.h"
#include "MidiPort.h"
#include "SourcePanel.h"
#include "Theme.h"
#include <iterator>
#include <memory>

namespace editor::ui {
class EditorComponent : public juce::Component, private juce::Timer, private juce::AsyncUpdater {
  public:
    EditorComponent();
    ~EditorComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ------------------------------------------------------------------ for --selftest
    // Port list entries: simulators first, then the MIDI outputs.
    juce::StringArray PortNames() const;
    void ConnectTo(int port_index);
    void Disconnect();
    // Manual clock instead of the timer: deterministic tests.
    void UseManualClock();
    void Step(uint32_t ms);
    bool LoadFile(const juce::File& file);
    bool SaveFile(const juce::File& file);
    Model& GetModel() { return model_; }
    Session& GetSession() { return session_; }
    Simulator* GetSimulator() { return simulator_.get(); }
    MatrixGrid& Grid() { return grid_; }
    SourcePanel& Source(int i) { return *sources_[std::size_t(i)]; }
    juce::Slider& DepthSlider() { return depth_; }
    juce::String LogText() const { return log_.getText(); }
    juce::TextEditor& NameField() { return name_; }
    // Applies pending view updates at once (the self-test has no message loop).
    void RefreshNow() { handleUpdateNowIfNeeded(); }

  private:
    struct Port {
        juce::String label;
        int simulator = -1;
        juce::MidiDeviceInfo out, in;
    };

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void Advance(uint32_t now_ms);
    void RefreshPorts();
    void RefreshAll();
    void RefreshCellEditor();
    void Log(LogKind kind, const juce::String& text);
    void OpenFileDialog();
    void SaveFileDialog();
    void ApplyLoaded(const LoadedPatch& loaded);

    LookAndFeel look_;
    Model model_;
    Session session_;
    std::unique_ptr<Simulator> simulator_;
    std::unique_ptr<MidiPort> midi_;
    std::vector<Port> ports_;
    bool manual_clock_ = false;
    uint32_t now_ = 0, last_ports_ = 0;
    juce::int64 last_alert_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::TooltipWindow tooltips_{this, 500};

    // header
    juce::ComboBox port_;
    juce::TextButton connect_{"Verbinden"};
    juce::Label status_;
    // grid card
    MatrixGrid grid_;
    juce::Label count_, hint_;
    // cell editor card
    juce::Label route_, cell_info_, device_heading_, device_info_;
    juce::Slider depth_;
    juce::ToggleButton enabled_{"eingeschaltet"};
    juce::TextButton zero_{"Auf 0"}, invert_{"Invertieren"};
    // sources
    std::array<std::unique_ptr<SourcePanel>, 3> sources_;
    // footer
    juce::Label name_label_;
    juce::TextEditor name_;
    juce::TextButton open_, save_, clear_{"Matrix leeren"}, pull_;
    juce::Label log_;

    juce::Rectangle<int> header_area_, grid_card_, cell_card_, footer_card_;
};
}
