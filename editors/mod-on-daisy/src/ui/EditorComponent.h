#pragma once
// The editor window's content (GUI.md § 2): status bar with the device, the grid, the
// cell editor, the source bar and the file row. Everything runs on the GUI thread;
// a 10 ms timer drives the session (timeouts, coalesced edits) and the simulator.
#include "../core/Bootloader.h"
#include "../core/PatchFile.h"
#include "../core/SimInstruments.h"
#include "../core/Simulator.h"
#include "MatrixGrid.h"
#include "MidiPort.h"
#include "SourcePanel.h"
#include "Theme.h"
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace editor::ui {
class EditorComponent : public juce::Component, private juce::Timer, private juce::AsyncUpdater {
  public:
    // with_simulators: the simulator ports are listed only for --selftest and --simulators;
    // in normal use the list holds MIDI ports alone (hardware test of 24.10.2026).
    explicit EditorComponent(bool with_simulators = false);
    ~EditorComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ------------------------------------------------------------------ for --selftest
    // Port list entries: simulators first (if enabled), then the MIDI outputs.
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
    bool PortOpen() const { return midi_ || simulator_; }
    // Firmware update: restart the kit into the Daisy bootloader (no confirmation dialog).
    void StartFirmwareUpdate(int port_index);
    bool FirmwareUpdateWaiting() const { return boot_.Waiting(); }
    // Answer to the compatibility question when loading a file (the dialog's buttons).
    enum class CompatChoice { Cancel = 0, Safe = 1, Force = 2 };
    // Set by the self-test: decides instead of the modal dialog.
    std::function<CompatChoice(const Compatibility&)> compat_decision;

    // ------------------------------------------------------------------ for the VST3 plugin
    // Connects on its own to the first Daisy port whenever one appears while offline,
    // unless the user pressed "Trennen" (GUI.md § 3).
    bool auto_connect = false;
    // Called on the message thread whenever the patch text changes (plugin state).
    std::function<void(const std::string&)> patch_changed;
    // The patch as a .modpatch text, as "Datei speichern" writes it.
    std::string PatchText() const;
    // The patch stored in the project: shown at once, sent to the device when connected
    // (now or at the next connection). Columns that mean something else on the device are
    // left out without asking, as "Ohne betroffene laden" does.
    void LoadProjectPatch(const std::string& text);

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
    void RefreshMode();
    void RefreshCellEditor();
    void Log(LogKind kind, const juce::String& text);
    void OpenFileDialog();
    void SaveFileDialog();
    void ApplyLoaded(const LoadedPatch& loaded, const mod::Patch& patch);
    void ApplyDecision(const LoadedPatch& loaded, const Compatibility& compat, CompatChoice choice);
    void ReleasePort();
    void AutoConnect();
    void SendProjectPatch(const LoadedPatch& loaded);
    void ConfirmFirmwareUpdate();
    void FinishFirmwareUpdate(BootRequest::Result result);

    LookAndFeel look_;
    Model model_;
    Session session_;
    std::unique_ptr<Simulator> simulator_;
    std::unique_ptr<MidiPort> midi_;
    BootRequest boot_;
    std::unique_ptr<MidiPort> boot_port_; // opened only for the request while offline
    std::vector<Port> ports_;
    bool with_simulators_ = false;
    bool manual_clock_ = false;
    bool user_disconnected_ = false;              // "Trennen" pressed: no automatic reconnection
    bool was_connected_ = false;                  // to notice the moment a connection is ready
    std::optional<LoadedPatch> pending_project_;  // project patch waiting for a connection
    std::string last_patch_text_;
    juce::String auto_tried_;                     // port list of the last automatic attempt
    uint32_t now_ = 0, last_ports_ = 0;
    juce::int64 last_alert_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::TooltipWindow tooltips_{this, 500};

    // header
    juce::ComboBox port_;
    juce::TextButton connect_{"Verbinden"};
    juce::TextButton update_;
    juce::Label status_;
    juce::Label mode_; // active instrument mode, e.g. "Modell: Western Chords"
    int mode_index_ = -1, mode_origin_ = -1; // from the last monitor frame
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
