#pragma once
// The control panel window's content (GUI.md § 2): status bar with the device, the grid, the
// cell editor, the source bar and the file row on the matrix page; the SD card page
// (SdSharePanel) beside it. Everything runs on the GUI thread; a 10 ms timer drives the
// sessions (timeouts, coalesced edits), the SD share workflow and the simulators.
#include "../core/Bootloader.h"
#include "../core/PatchFile.h"
#include "../core/SdShare.h"
#include "../core/SdShareSim.h"
#include "../core/SimInstruments.h"
#include "../core/Simulator.h"
#include "../core/SqSimulator.h"
#include "../core/StreichwurstSim.h"
#include "MatrixGrid.h"
#include "MidiPort.h"
#include "SdSharePanel.h"
#include "SqPanel.h"
#include "SourcePanel.h"
#include "Theme.h"
#include <deque>
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
    // sd_page: the SD card page is available (standalone app); the VST3 shows the D2 note.
    explicit EditorComponent(bool with_simulators = false, bool sd_page = true);
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
    // Acid Seq (SQ protocol): the session and its pages; active while an SQ device is read or connected.
    SqSession& GetSqSession() { return sq_session_; }
    SqPanel& GetSqPanel() { return sq_panel_; }
    SqSimulator* GetSqSimulator() { return sq_sim_.get(); }
    StreichwurstSim* GetStreichwurstSimulator() { return sw_sim_.get(); }
    // Streichwurst: Mod on Daisy runs alongside SQ on the same port; this shows its pages.
    void ShowMatrix(bool on);
    bool MatrixShown() const { return show_matrix_; }
    bool SqActive() const {
        return sq_session_.GetPhase() == SqSession::Phase::Reading || sq_session_.GetPhase() == SqSession::Phase::Ready;
    }
    // Answer to "send the project's Acid Seq patch?" (VST3 song state): 0 no, 1 yes, 2 always.
    // Set by the self-test; otherwise a dialog asks.
    std::function<int(const AcidCompat&)> sq_upload_decision;
    Simulator* GetSimulator() { return simulator_.get(); }
    MatrixGrid& Grid() { return grid_; }
    SourcePanel& Source(int i) { return *sources_[std::size_t(i)]; }
    juce::Slider& DepthSlider() { return depth_; }
    juce::String LogText() const { return log_.getText(); }
    juce::TextEditor& NameField() { return name_; }
    // Applies pending view updates at once (the self-test has no message loop).
    void RefreshNow() { handleUpdateNowIfNeeded(); }
    bool PortOpen() const { return midi_ || simulator_ || sd_sim_midi_; }
    // Firmware update: restart the kit into the Daisy bootloader (no confirmation dialog).
    void StartFirmwareUpdate(int port_index);
    bool FirmwareUpdateWaiting() const { return boot_.Waiting(); }
    // Answer to the compatibility question when loading a file (the dialog's buttons).
    enum class CompatChoice { Cancel = 0, Safe = 1, Force = 2 };
    // Set by the self-test: decides instead of the modal dialog.
    std::function<CompatChoice(const Compatibility&)> compat_decision;

    // ------------------------------------------------------------------ SD card page
    // The share/return workflow (core/SdShare.h) and its CDC session; the SD card page.
    sd::SdShareWorkflow& GetSdShare() { return sd_; }
    ddp::DdpSession& GetDdpSession() { return ddp_session_; }
    SdSharePanel& GetSdPanel() { return sd_panel_; }
    // The simulated instrument behind the "Simulator: Nebulae …" / "… Looper …" ports.
    sd::SimInstrument* GetSdSimulator() { return sd_sim_.get(); }
    void ShowSdPage(bool on);
    bool SdPageShown() const { return show_sd_; }
    // Share from the page: saves the patch first (D5), then asks the workflow.
    bool RequestSdShare(bool confirm);
    // Set by the self-test instead of the "Interrupted SD access" question: true = continue.
    std::function<bool()> sd_ack_decision;
    // Self-test: receives the question's continuation instead of a dialog, to answer it later.
    std::function<void(std::function<void(bool)>)> sd_ack_prompt;
    // Self-test and simulators only: no Windows COM port is ever opened (review finding 12).
    void DisableHardwareSerial();
    bool HardwareSerialActive() const { return serial_ports_ != nullptr; }
    // The matrix of the connected kit is still being detected or read (Share waits, D5).
    bool MatrixDetectionPending() const;
    // Autosave written synchronously (self-test with a manual clock) instead of on a worker.
    bool sd_autosave_sync = false;
    // Where the patch is saved before a share; default %LOCALAPPDATA%\Daisy Control Panel\autosave.
    juce::File sd_autosave_dir;
    juce::File LastAutosave() const { return sd_autosave_file_; }
    bool SdRestorePending() const { return sd_restore_pending_; }
    juce::TextButton& ConnectButton() { return connect_; }
    // "Disconnect" is offered while any session or the SD share workflow holds the port.
    bool Busy() const;
    juce::String NoMatrixText() const { return no_matrix_.isVisible() ? no_matrix_.getText() : juce::String(); }
    // The about line of the SD card page (app name, version, licence), set by the application.
    void SetAbout(const juce::String& text);

    // ------------------------------------------------------------------ for the VST3 plugin
    // Connects on its own to the first Daisy port whenever one appears while offline,
    // unless the user pressed "Disconnect" (GUI.md § 3).
    bool auto_connect = false;
    // Called on the message thread whenever the patch text changes (plugin state).
    std::function<void(const std::string&)> patch_changed;
    // The patch as a .modpatch text, as "Save file" writes it.
    std::string PatchText() const;
    // The patch stored in the project: shown at once, sent to the device when connected
    // (now or at the next connection). Columns that mean something else on the device are
    // left out without asking, as "Load without affected" does.
    void LoadProjectPatch(const std::string& text);

  private:
    struct Port {
        juce::String label;
        int simulator = -1;
        juce::MidiDeviceInfo out, in;
    };

    // The workflow's view of the shared MIDI port: SysEx of the SD share protocols is
    // queued here by Advance(); Close() only detaches, the port stays with the matrix.
    class SdLink : public sd::MidiLink {
      public:
        explicit SdLink(EditorComponent& owner) : owner_(owner) {}
        ~SdLink() override { Detach(); }
        bool Send(const uint8_t* bytes, std::size_t n) override { return !closed_ && owner_.SendSdSysEx(bytes, n, *this); }
        // Messages that arrived before the port vanished (the SHARE ACK just before the
        // restart) still reach the workflow; only an empty poll on a dead port is a loss.
        bool Poll(std::vector<std::vector<uint8_t>>& out) override {
            out.swap(queue_);
            queue_.clear();
            return !out.empty() || (!closed_ && owner_.SdPortAlive());
        }
        void Close() override {
            closed_ = true;
            Detach();
        }
        void Push(const uint8_t* bytes, std::size_t n) {
            if(queue_.size() < 256)
                queue_.emplace_back(bytes, bytes + n);
        }

      private:
        void Detach() {
            if(owner_.sd_link_ == this)
                owner_.sd_link_ = nullptr;
        }
        EditorComponent& owner_;
        std::vector<std::vector<uint8_t>> queue_;
        bool closed_ = false;
    };
    // MIDI ports as the workflow sees them (SD_SHARE_PORT.md § 7).
    class SdMidiPorts : public sd::MidiPorts {
      public:
        explicit SdMidiPorts(EditorComponent& owner) : owner_(owner) {}
        std::vector<std::string> List() override { return owner_.SdPortCandidates(); }
        std::unique_ptr<sd::MidiLink> Open(const std::string& name, std::string& error) override {
            return owner_.OpenSdLink(name, error);
        }

      private:
        EditorComponent& owner_;
    };
    friend class SdLink;
    friend class SdMidiPorts;

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void Advance(uint32_t now_ms);
    void RefreshPorts();
    void RefreshAll();
    void RefreshMode();
    void RefreshCellEditor();
    void RefreshPages();
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
    void AskProjectUpload();
    // SD card page
    std::vector<std::string> SdPortCandidates();
    std::unique_ptr<sd::MidiLink> OpenSdLink(const std::string& name, std::string& error);
    bool SendSdSysEx(const uint8_t* bytes, std::size_t n, SdLink& link);
    bool SdPortAlive() const;
    void OnSdPhase(sd::Phase before, sd::Phase now);
    bool SdInstrumentFound() const;
    bool SaveBeforeShare();
    void RestoreAfterShare();
    void SdEjectHelp();
    void SdAcknowledge();
    juce::String CurrentPortLabel() const;
    void RouteMidiFrames();

    // Sends to whatever carries the SQ conversation: the MIDI port or the SQ simulator.
    struct SqLink : Transport {
        std::function<bool(const uint8_t*, std::size_t)> send;
        bool Send(const uint8_t* bytes, std::size_t size) override { return send && send(bytes, size); }
    };

    LookAndFeel look_;
    Model model_;
    Session session_;
    std::unique_ptr<Simulator> simulator_;
    SqLink sq_link_;
    SqSession sq_session_{sq_link_};
    std::unique_ptr<SqSimulator> sq_sim_;
    std::unique_ptr<StreichwurstSim> sw_sim_;      // "Streichwurst (Sim)": SQ and MD on one port
    SqLink md_link_;                                // the MD session's way to that simulator
    std::deque<std::vector<uint8_t>> sq_sim_out_;
    bool md_alongside_ = false; // SQ 1.1 device with CapModMatrix: MD session started on its port
    bool show_matrix_ = false;  // matrix pages instead of the SQ pages
    SqPanel sq_panel_{sq_session_};
    bool md_pending_ = false;        // real port: Mod on Daisy is tried once SQ did not answer
    bool sq_was_ready_ = false;
    bool sq_always_upload_ = false;  // VST3: "Always send" chosen for this plugin instance
    std::optional<AcidPatch> sq_project_; // Acid Seq song state waiting for a device and a decision
    std::unique_ptr<MidiPort> midi_;
    BootRequest boot_;
    std::unique_ptr<MidiPort> boot_port_; // opened only for the request while offline
    std::vector<Port> ports_;
    bool with_simulators_ = false;
    bool manual_clock_ = false;
    bool user_disconnected_ = false;              // "Disconnect" pressed: no automatic reconnection
    bool was_connected_ = false;                  // to notice the moment a connection is ready
    std::optional<LoadedPatch> pending_project_;  // project patch waiting for a connection
    std::string last_patch_text_;
    juce::String auto_tried_;                     // port list of the last automatic attempt
    uint32_t now_ = 0, last_ports_ = 0;
    juce::int64 last_alert_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::TooltipWindow tooltips_{this, 500};

    // SD card page: CDC session (real COM ports or the simulated cable), the workflow on the
    // shared MIDI port, the simulated instrument, the page.
    std::unique_ptr<ddp::SerialPorts> serial_ports_;
    ddp::DdpSession ddp_session_{nullptr};
    SdMidiPorts sd_midi_ports_{*this};
    sd::SdShareWorkflow sd_{ddp_session_, &sd_midi_ports_};
    SdLink* sd_link_ = nullptr;                     // the workflow's current link, owned by the workflow
    std::unique_ptr<sd::SimInstrument> sd_sim_;     // "Simulator: Nebulae …" / "… Looper …"
    std::unique_ptr<sd::SimSerialPorts> sd_sim_serial_;
    std::unique_ptr<sd::SimMidiLink> sd_sim_midi_;  // the simulated USB-MIDI side of sd_sim_, open like a port
    int sd_sim_port_ = -1;                          // Port::simulator id of the SD simulator in use
    bool sd_page_available_ = true;
    bool show_sd_ = false;
    bool opening_sd_link_ = false;                  // ConnectTo() runs for the workflow: keep its state
    bool sd_reconnected_ = false;                   // the current port was opened by the workflow (kit coming back)
    bool sd_ambiguous_logged_ = false;              // "several ports with this name" said once per share
    std::string sd_restore_key_;                    // instrument key the saved patch belongs to
    juce::String sd_share_port_id_;                 // JUCE identifier of the port the share started on
    juce::String sd_open_identifier_;               // OpenSdLink(): the exact port entry to connect to
    bool sd_auto_switched_ = false;                 // D4: switched to the SD page for this device
    sd::Phase last_sd_phase_ = sd::Phase::NotFound;
    std::optional<LoadedPatch> sd_restore_;         // the patch saved before the share (D5)
    juce::File sd_autosave_file_;
    bool sd_restore_pending_ = false;               // a restart the workflow caused: send sd_restore_ on reconnect
    SdSharePanel sd_panel_;

    // header
    juce::ComboBox port_;
    juce::TextButton connect_{"Connect"};
    juce::TextButton update_;
    juce::TextButton view_; // Streichwurst: "Matrix" / "Sound parameters"
    juce::TextButton page_matrix_{"Matrix"}, page_sd_{"SD card"};
    juce::Label status_;
    juce::Label mode_; // active instrument mode, e.g. "Model: Western Chords"
    int mode_index_ = -1, mode_origin_ = -1; // from the last monitor frame
    // grid card
    MatrixGrid grid_;
    juce::Label count_, hint_, no_matrix_;
    // cell editor card
    juce::Label route_, cell_info_, device_heading_, device_info_;
    juce::Slider depth_;
    juce::ToggleButton enabled_{"enabled"};
    juce::TextButton zero_{"Set to 0"}, invert_{"Invert"};
    // sources
    std::array<std::unique_ptr<SourcePanel>, 3> sources_;
    // footer
    juce::Label name_label_;
    juce::TextEditor name_;
    juce::TextButton open_, save_, clear_{"Clear matrix"}, pull_;
    juce::Label log_;

    juce::Rectangle<int> header_area_, grid_card_, cell_card_, footer_card_;
};
}
