#include "EditorComponent.h"
// The standalone app (MOD_EDITOR_SD_PAGE) talks to the kit's COM port; the VST3 does not (D2).
#if defined(_WIN32) && defined(MOD_EDITOR_SD_PAGE)
#include "SerialPortWin.h"
#endif

namespace editor::ui {
namespace {
constexpr int kHeader = 56, kCellCard = 300, kSourcesHeight = 250, kFooter = 56, kPad = 14;
constexpr int kSqSimulator = -2; // Port::simulator of "Acid Seq (Sim)"
constexpr int kSwSimulator = -3; // Port::simulator of "Streichwurst (Sim)"
// Instruments that share their SD card, simulated with both USB personalities
// (core/SdShareSim.h); the MOD variants add the matrix of core/SimInstruments.h.
constexpr int kSdNebulaeMod = -4, kSdLooperMod = -5, kSdNebulae = -6, kSdLooper = -7;
constexpr const char* kSdNebulaeLabel = "Simulator: Nebulae", *kSdLooperLabel = "Simulator: Looper";
bool IsSdSimulator(int id) { return id <= kSdNebulaeMod && id >= kSdLooper; }
const sd::Profile& SdSimProfile(int id) { return id == kSdNebulaeMod || id == kSdNebulae ? sd::kNebulae : sd::kLooper; }
int SdSimMatrix(int id) { return id == kSdNebulaeMod ? 0 : id == kSdLooperMod ? 1 : -1; } // index into kSdSimInstruments

juce::Colour LogColour(LogKind k) {
    switch(k) {
        case LogKind::Ok: return colour::ok;
        case LogKind::Warn: return colour::warn;
        case LogKind::Error: return colour::err;
        case LogKind::Info: break;
    }
    return colour::ink;
}
}

EditorComponent::EditorComponent(bool with_simulators, bool sd_page)
    : session_(model_, uint32_t(juce::Random::getSystemRandom().nextInt())), with_simulators_(with_simulators),
      sd_page_available_(sd_page), sd_panel_(sd_, ddp_session_, sd_page, {}), grid_(model_, session_) {
    setLookAndFeel(&look_);
    // SD card page (SD_SHARE_PORT.md § 7): the CDC side scans the kit's COM ports on its own;
    // the MIDI side shares the port of the matrix. The VST3 has neither (D2).
    if(sd_page_available_) {
#if defined(_WIN32) && defined(MOD_EDITOR_SD_PAGE)
        serial_ports_ = ddp::MakeWindowsSerialPorts();
        ddp_session_.SetPorts(serial_ports_.get());
#endif
    }
    ddp_session_.log = [this](const std::string& text) { Log(LogKind::Info, U(text)); };
    ddp_session_.changed = [this] { triggerAsyncUpdate(); };
    sd_.log = [this](const std::string& text) { Log(LogKind::Info, U(text)); };
    sd_.changed = [this] { triggerAsyncUpdate(); };
    sd_panel_.request_share = [this](bool confirm) { RequestSdShare(confirm); };
    sd_panel_.cancel = [this] {
        sd_.CancelConfirmation();
        RefreshAll();
    };
    sd_panel_.return_to_performance = [this] {
        sd_.ReturnToPerformance(); // the workflow logs the request itself
        RefreshAll();
    };
    sd_panel_.acknowledge = [this] { SdAcknowledge(); };
    sd_panel_.eject_help = [this] { SdEjectHelp(); };
    sd_panel_.share_blocked = [this] { return MatrixDetectionPending(); };
    for(juce::TextButton* b : {&page_matrix_, &page_sd_}) {
        addAndMakeVisible(b);
        b->setClickingTogglesState(false);
    }
    page_matrix_.setTooltip(U("Modulation matrix of the connected instrument"));
    page_sd_.setTooltip(U("Share the instrument's microSD card with Windows and bring it back (Nebulae on Daisy, Looper)"));
    page_matrix_.onClick = [this] { ShowSdPage(false); };
    page_sd_.onClick = [this] { ShowSdPage(true); };
    addChildComponent(no_matrix_);
    no_matrix_.setColour(juce::Label::textColourId, colour::warn);
    no_matrix_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    no_matrix_.setJustificationType(juce::Justification::centred);
    session_.changed = [this] { triggerAsyncUpdate(); };
    session_.log = [this](LogKind k, const std::string& text) { Log(k, U(text)); };
    session_.monitor = [this](const MonitorValues& m) {
        grid_.SetMonitor(m);
        if(m.mode != mode_index_ || m.mode_origin != mode_origin_) {
            mode_index_ = m.mode;
            mode_origin_ = m.mode_origin;
            RefreshMode();
        }
        for(int i = 0; i < 3; ++i)
            sources_[std::size_t(i)]->SetClockState(
                m.ClockState(model_.SourceParam(i, mod::ParamSync), model_.SourceParam(i, mod::ParamGate)));
    };

    sq_session_.changed = [this] { triggerAsyncUpdate(); };
    sq_session_.log = [this](LogKind k, const std::string& text) { Log(k, U(text)); };
    addAndMakeVisible(port_);
    port_.setTooltip(U("Device: the Daisy's MIDI port (“Daisy Seed External”)"));
    addAndMakeVisible(connect_);
    connect_.setColour(juce::TextButton::buttonColourId, colour::accent);
    connect_.onClick = [this] {
        if(Busy()) {
            user_disconnected_ = true; // the plugin stays offline until "Connect"
            Disconnect();
        } else {
            ConnectTo(port_.getSelectedItemIndex());
        }
    };
    addAndMakeVisible(update_);
    update_.setButtonText(U("Firmware update …"));
    update_.setTooltip(U("Restarts the kit into the Daisy bootloader, without BOOT and RESET. Then load the firmware "
                         "with the Daisy Web Programmer or “make program-dfu”."));
    update_.onClick = [this] { ConfirmFirmwareUpdate(); };
    addChildComponent(view_);
    view_.setTooltip(U("Streichwurst: sound parameters (SQ) and modulation matrix (Mod on Daisy) of the same device"));
    view_.onClick = [this] { ShowMatrix(!show_matrix_); };
    sq_panel_.matrix_text = [this] { return session_.Connected() ? SavePatch(model_, &session_.Device()) : std::string(); };
    sq_panel_.upload_matrix = [this](const std::string& text) {
        const LoadedPatch loaded = LoadPatch(text, model_);
        if(!loaded.ok || !session_.Connected())
            return Log(LogKind::Warn, U("Matrix from the patch not sent: ") +
                                          U(loaded.ok ? std::string("no Mod on Daisy connection") : loaded.error));
        SendProjectPatch(loaded); // columns that mean something else stay off
    };
    addAndMakeVisible(status_);
    addAndMakeVisible(mode_);
    mode_.setColour(juce::Label::textColourId, colour::ink);
    status_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(grid_);
    grid_.selected = [this](int) { RefreshCellEditor(); };
    addAndMakeVisible(count_);
    addAndMakeVisible(hint_);
    hint_.setColour(juce::Label::textColourId, colour::muted);
    hint_.setFont(juce::FontOptions(12.0f));
    hint_.setText(U("Select a cell · drag vertically to change the depth (Shift fine) · mouse wheel ±1 (Shift ±10) · "
                    "double-click on/off · right-click menu · arrows, +/−, Page↑/↓ ±10, 0, Space"),
                  juce::dontSendNotification);

    for(juce::Label* l : {&route_, &cell_info_, &device_heading_, &device_info_})
        addAndMakeVisible(l);
    route_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    cell_info_.setColour(juce::Label::textColourId, colour::muted);
    cell_info_.setJustificationType(juce::Justification::topLeft);
    device_heading_.setText(U("DEVICE"), juce::dontSendNotification);
    device_heading_.setColour(juce::Label::textColourId, colour::muted);
    device_heading_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    device_info_.setColour(juce::Label::textColourId, colour::muted);
    device_info_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(depth_);
    depth_.setRange(-100, 100, 1);
    depth_.setSliderStyle(juce::Slider::LinearHorizontal);
    depth_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
    depth_.setDoubleClickReturnValue(true, 0);
    depth_.onValueChange = [this] { session_.EditDepth(model_.selected, int(depth_.getValue())); };
    addAndMakeVisible(enabled_);
    enabled_.onClick = [this] {
        session_.EditDepth(model_.selected, model_.Depth(model_.selected), enabled_.getToggleState());
    };
    for(juce::TextButton* b : {&zero_, &invert_})
        addAndMakeVisible(b);
    zero_.onClick = [this] { session_.EditDepth(model_.selected, 0); };
    invert_.onClick = [this] { session_.EditDepth(model_.selected, -model_.Depth(model_.selected)); };

    for(int i = 0; i < 3; ++i) {
        sources_[std::size_t(i)] = std::make_unique<SourcePanel>(model_, session_, i);
        addAndMakeVisible(*sources_[std::size_t(i)]);
    }

    addAndMakeVisible(name_label_);
    name_label_.setText("Patch", juce::dontSendNotification);
    addAndMakeVisible(name_);
    name_.setInputRestrictions(16, " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
                                   "abcdefghijklmnopqrstuvwxyz{|}~");
    name_.setTextToShowWhenEmpty("Name", colour::muted);
    name_.setJustification(juce::Justification::centredLeft);
    name_.setIndents(8, 0);
    name_.onReturnKey = name_.onFocusLost = [this] {
        if(name_.getText().toStdString() != model_.Name())
            session_.EditName(name_.getText().toStdString());
    };
    open_.setButtonText(U("Open file …"));
    save_.setButtonText(U("Save file …"));
    pull_.setButtonText(U("Load from device"));
    for(juce::TextButton* b : {&open_, &save_, &clear_, &pull_})
        addAndMakeVisible(b);
    open_.onClick = [this] { OpenFileDialog(); };
    save_.onClick = [this] { SaveFileDialog(); };
    clear_.onClick = [this] { session_.ClearMatrix(); };
    pull_.setTooltip(U("Reads the matrix, sources and name again from the device, e.g. after changes made with another "
                       "program. Without such changes everything stays the same."));
    pull_.onClick = [this] {
        session_.Pull(false, [this] {
            Log(LogKind::Ok, U("State loaded from the device (patch CRC ") + U(Hex32(model_.Crc())) + ").");
        });
    };
    addAndMakeVisible(log_);
    log_.setFont(juce::FontOptions(13.0f));
    addChildComponent(sq_panel_); // over the matrix pages while an Acid Seq device is connected
    addChildComponent(sd_panel_); // over everything while the SD card page is shown (added last: on top)

    RefreshPorts();
    RefreshAll();
    Log(LogKind::Info, with_simulators_ ? U("Not connected. Without a device you can edit offline or choose a simulator.")
                                        : U("Not connected. Without a device you can edit offline."));
    setSize(1320, 920);
    startTimer(10);
}

EditorComponent::~EditorComponent() {
    stopTimer();
    session_.Disconnect("");
    sd_.ReleaseMidi();
    ddp_session_.Disconnect("Application closed"); // BYE only: closing never returns the card
    setLookAndFeel(nullptr);
}

// ------------------------------------------------------------------ ports and connection
void EditorComponent::RefreshPorts() {
    const auto outs = juce::MidiOutput::getAvailableDevices();
    const auto ins = juce::MidiInput::getAvailableDevices();
    std::vector<Port> ports;
    for(int i = 0; with_simulators_ && i < int(std::size(kSimInstruments)); ++i)
        ports.push_back({U(kSimInstruments[i].label), i, {}, {}});
    if(with_simulators_) {
        ports.push_back({U("Acid Seq (Sim)"), kSqSimulator, {}, {}});
        ports.push_back({U("Streichwurst (Sim)"), kSwSimulator, {}, {}});
        ports.push_back({U(kSdSimInstruments[0].label), kSdNebulaeMod, {}, {}});
        ports.push_back({U(kSdSimInstruments[1].label), kSdLooperMod, {}, {}});
        ports.push_back({U(kSdNebulaeLabel), kSdNebulae, {}, {}});
        ports.push_back({U(kSdLooperLabel), kSdLooper, {}, {}});
    }
    for(const auto& out : outs) {
        // The input with the same name, else one that also says "Daisy".
        juce::MidiDeviceInfo in;
        for(const auto& candidate : ins)
            if(candidate.name == out.name)
                in = candidate;
        if(in.identifier.isEmpty())
            for(const auto& candidate : ins)
                if(candidate.name.containsIgnoreCase("daisy") && out.name.containsIgnoreCase("daisy"))
                    in = candidate;
        if(in.identifier.isNotEmpty())
            ports.push_back({out.name, -1, out, in});
    }
    bool same = ports.size() == ports_.size();
    for(std::size_t i = 0; same && i < ports.size(); ++i)
        same = ports[i].label == ports_[i].label && ports[i].out.identifier == ports_[i].out.identifier;
    if(midi_) { // the open output or input vanished: unplugged
        bool out_present = false, in_present = false;
        for(const auto& out : outs)
            out_present = out_present || out.identifier == midi_->Output().identifier;
        for(const auto& in : ins)
            in_present = in_present || in.identifier == midi_->Input().identifier;
        if(!out_present || !in_present) {
            RouteMidiFrames(); // the last replies (a SHARE ACK just before the restart) still count
            Disconnect();
            session_.Disconnect(U("device disconnected").toStdString());
            Log(LogKind::Error, U("The connection to the Daisy was lost. Editing continues offline."));
        }
    }
    if(same)
        return;
    const juce::String previous = port_.getText();
    // While offline, a Daisy that appears later replaces a non-Daisy suggestion (the kit is
    // often plugged in or leaves the bootloader after the editor started).
    const bool keep = midi_ || simulator_ || sq_sim_ || sw_sim_ || sd_sim_ || previous.containsIgnoreCase("daisy");
    ports_ = std::move(ports);
    port_.clear(juce::dontSendNotification);
    int same_label = -1, daisy = -1;
    for(std::size_t i = 0; i < ports_.size(); ++i) {
        port_.addItem(ports_[i].label, int(i) + 1);
        if(same_label < 0 && previous.isNotEmpty() && ports_[i].label == previous)
            same_label = int(i);
        if(daisy < 0 && ports_[i].simulator == -1 && ports_[i].label.containsIgnoreCase("daisy"))
            daisy = int(i); // a connected Daisy is the first suggestion
    }
    const int choose = (keep && same_label >= 0) ? same_label : daisy >= 0 ? daisy : std::max(same_label, 0);
    port_.setSelectedItemIndex(choose, juce::dontSendNotification);
}

juce::StringArray EditorComponent::PortNames() const {
    juce::StringArray names;
    for(const Port& p : ports_)
        names.add(p.label);
    return names;
}

void EditorComponent::ConnectTo(int index) {
    Disconnect();
    if(!opening_sd_link_) { // the user's choice: not the kit coming back from a share
        user_disconnected_ = false;
        if(sd_restore_pending_) {
            sd_restore_pending_ = false;
            Log(LogKind::Warn, U("The patch saved before the share is not sent to a device you chose yourself; open it from ")
                                   + sd_autosave_file_.getFullPathName());
        }
    }
    if(index < 0 || index >= int(ports_.size()))
        return Log(LogKind::Warn, U("Please choose a device."));
    const Port& p = ports_[std::size_t(index)];
    if(!IsSdSimulator(p.simulator) && sd_sim_) { // another port: the simulated kit is unplugged
        ddp_session_.Disconnect("Simulated kit unplugged");
        ddp_session_.SetPorts(serial_ports_.get());
        sd_sim_serial_.reset();
        sd_sim_.reset();
        sd_sim_port_ = -1;
    }
    if(IsSdSimulator(p.simulator)) {
        if(!sd_sim_ || sd_sim_port_ != p.simulator) {
            ddp_session_.Disconnect("");
            sd_sim_ = std::make_unique<sd::SimInstrument>(SdSimProfile(p.simulator), [this] { return now_; });
            sd_sim_serial_ = std::make_unique<sd::SimSerialPorts>(*sd_sim_);
            sd_sim_port_ = p.simulator;
            ddp_session_.SetPorts(sd_sim_serial_.get());
        }
        if(!sd_sim_->MidiAlive()) {
            port_.setSelectedItemIndex(index, juce::dontSendNotification);
            RefreshAll();
            return Log(LogKind::Warn, U("The simulated kit has no USB-MIDI port now (SD management or restarting); use the SD card page."));
        }
        sd_sim_midi_ = std::make_unique<sd::SimMidiLink>(*sd_sim_); // the port is open
        const int matrix = SdSimMatrix(p.simulator);
        if(matrix >= 0) { // the MOD build: the matrix on the same simulated port
            const SimInstrument& s = kSdSimInstruments[matrix];
            simulator_ = std::make_unique<Simulator>(s.make(), uint32_t(juce::Random::getSystemRandom().nextInt()));
            simulator_->bases = s.bases;
            simulator_->mode = s.mode;
            simulator_->EnableExtensions(s.extensions);
            session_.Connect(*simulator_, now_);
        } else {
            session_.Disconnect("no matrix");
        }
    } else if(p.simulator == kSqSimulator) {
        sq_sim_ = std::make_unique<SqSimulator>("acid-seq");
        sq_sim_->SetOut([this](const uint8_t* b, std::size_t n) { sq_sim_out_.emplace_back(b, b + n); });
        sq_link_.send = [this](const uint8_t* b, std::size_t n) {
            sq_sim_->Receive(b, n, now_);
            return true;
        };
        sq_session_.Connect(now_);
    } else if(p.simulator == kSwSimulator) {
        sw_sim_ = std::make_unique<StreichwurstSim>();
        sw_sim_->SetOut([this](const uint8_t* b, std::size_t n) { sq_sim_out_.emplace_back(b, b + n); });
        sq_link_.send = [this](const uint8_t* b, std::size_t n) {
            sw_sim_->Receive(b, n, now_);
            return true;
        };
        md_link_.send = sq_link_.send;
        sq_session_.Connect(now_);
    } else if(p.simulator >= 0) {
        simulator_ = std::make_unique<Simulator>(kSimInstruments[p.simulator].make(),
                                                 uint32_t(juce::Random::getSystemRandom().nextInt()));
        simulator_->bases = kSimInstruments[p.simulator].bases;
        simulator_->mode = kSimInstruments[p.simulator].mode;
        simulator_->EnableExtensions(kSimInstruments[p.simulator].extensions);
        session_.Connect(*simulator_, now_);
    } else {
        juce::String error;
        midi_ = MidiPort::Open(p.out, p.in, error);
        if(!midi_)
            return Log(LogKind::Error, error);
        // SQ first (Acid Seq firmwares); without an answer Mod on Daisy follows (R09).
        sq_link_.send = [this](const uint8_t* b, std::size_t n) { return midi_ && midi_->Send(b, n); };
        sq_session_.Connect(now_, 1);
        md_pending_ = true;
    }
    port_.setSelectedItemIndex(index, juce::dontSendNotification);
    last_alert_ = 0; // messages of the previous device are over
    sd_.ScanNow();   // the SD share workflow probes the new port at once (NB/LP STATUS)
    Log(LogKind::Info, U("Connecting to ") + p.label + U(" …"));
    RefreshAll();
}

void EditorComponent::ShowMatrix(bool on) {
    show_matrix_ = on && sq_session_.Ready() && sq_session_.Identity().HasModMatrix();
    RefreshAll();
    resized();
}

void EditorComponent::Disconnect() {
    sd_reconnected_ = false;
    md_pending_ = false;
    md_alongside_ = false;
    show_matrix_ = false;
    sq_session_.Disconnect();
    session_.Disconnect("");
    // The SD share workflow loses its MIDI link with the port; the card reader is never
    // touched by this (its CDC session stays as it is).
    if(!opening_sd_link_)
        sd_.ReleaseMidi();
    ReleasePort();
}

void EditorComponent::ShowSdPage(bool on) {
    show_sd_ = on;
    RefreshAll();
    resized();
}

// ------------------------------------------------------------------ clock
void EditorComponent::UseManualClock() {
    manual_clock_ = true;
    stopTimer();
}

void EditorComponent::Step(uint32_t ms) {
    for(uint32_t i = 0; i < ms; ++i)
        Advance(now_ + 1);
}

void EditorComponent::timerCallback() {
    if(manual_clock_)
        return;
    const uint32_t now = juce::Time::getMillisecondCounter();
    if(now_ == 0)
        now_ = now - 1;
    // The simulator runs in 1 ms steps; after a stall it catches up at most 100 ms.
    const uint32_t elapsed = std::min<uint32_t>(now - now_, 100);
    for(uint32_t i = 1; i < elapsed; ++i)
        Advance(now - elapsed + i);
    Advance(now);
    if(now - last_ports_ >= 1000) {
        last_ports_ = now;
        grid_.CheckStale();
        if(!port_.isPopupActive())
            RefreshPorts();
        AutoConnect();
    }
}

// Plugin: connect once per change of the port list (start, kit plugged in, back from the
// bootloader), so a busy port does not produce a new error every second.
void EditorComponent::AutoConnect() {
    if(!auto_connect || user_disconnected_ || session_.State() != LinkState::Offline || PortOpen() || boot_.Waiting())
        return;
    const juce::String list = PortNames().joinIntoString("|");
    if(list == auto_tried_)
        return;
    auto_tried_ = list;
    for(std::size_t i = 0; i < ports_.size(); ++i)
        if(ports_[i].simulator == -1 && ports_[i].label.containsIgnoreCase("daisy")) {
            port_.setSelectedItemIndex(int(i), juce::dontSendNotification);
            ConnectTo(int(i));
            return;
        }
}

std::string EditorComponent::PatchText() const {
    if(sq_session_.Ready()) // an SQ device: the product patch (Streichwurst: with matrix) is the song state
        return SerializeAcidPatch(sq_panel_.CurrentPatch());
    return SavePatch(model_, session_.Connected() ? &session_.Device() : nullptr);
}

void EditorComponent::LoadProjectPatch(const std::string& text) {
    if(text.find("\"acid-seq-patch\"") != std::string::npos || text.find("\"streichwurst-patch\"") != std::string::npos) {
        std::string error;
        sq_project_ = ParseAcidPatch(text, error);
        if(!sq_project_)
            return Log(LogKind::Error, U("Product patch in the project is not readable: ") + U(error));
        if(sq_session_.Ready())
            AskProjectUpload();
        else
            Log(LogKind::Info, U("Product patch loaded from the project; on connecting you will be asked whether to send it."));
        return;
    }
    const LoadedPatch loaded = LoadPatch(text, model_);
    if(!loaded.ok) {
        Log(LogKind::Error, U("Patch in the project is not readable: ") + U(loaded.error));
        return;
    }
    if(session_.Connected()) {
        SendProjectPatch(loaded);
        return;
    }
    ApplyLoaded(loaded, loaded.patch); // shown offline at once
    pending_project_ = loaded;
    Log(LogKind::Info, U("Patch loaded from the project; it goes to the device on connecting."));
}

// The project wins over what the device holds; mismatching columns stay off (F1, "safe").
void EditorComponent::SendProjectPatch(const LoadedPatch& loaded) {
    const Compatibility compat = CheckCompatibility(loaded, model_, session_.Device());
    ApplyDecision(loaded, compat, compat.NeedsDecision() ? CompatChoice::Safe : CompatChoice::Force);
    Log(LogKind::Ok, U("Patch from the project sent to the device."));
}

void EditorComponent::Advance(uint32_t now_ms) {
    now_ = now_ms;
    if(simulator_)
        simulator_->Advance(1, [this](const uint8_t* b, std::size_t n) { session_.OnMessage(b, n); });
    if(sq_sim_ || sw_sim_) {
        if(sq_sim_)
            sq_sim_->Advance(now_);
        if(sw_sim_)
            sw_sim_->Advance(now_);
        while(!sq_sim_out_.empty()) {
            const std::vector<uint8_t> m = std::move(sq_sim_out_.front());
            sq_sim_out_.pop_front();
            if(ClassifySysEx(m.data(), m.size()) == Protocol::Sq)
                sq_session_.OnMessage(m.data(), m.size(), now_);
            else
                session_.OnMessage(m.data(), m.size());
        }
    }
    RouteMidiFrames();
    if(sd_sim_midi_) { // the simulated instrument's control SysEx replies
        std::vector<std::vector<uint8_t>> replies;
        if(sd_sim_midi_->Poll(replies))
            for(const std::vector<uint8_t>& m : replies)
                if(sd_link_)
                    sd_link_->Push(m.data(), m.size());
    }
    sq_session_.Tick(now_);
    // SQ 1.1 with CapModMatrix (Streichwurst): Mod on Daisy alongside on the same port,
    // started once the SQ state has been read; it reads the matrix first as well.
    if(!md_alongside_ && sq_session_.Ready() && sq_session_.Identity().HasModMatrix() &&
       session_.State() == LinkState::Offline && (midi_ || sw_sim_)) {
        md_alongside_ = true;
        if(midi_)
            session_.Connect(*midi_, now_);
        else
            session_.Connect(md_link_, now_);
    }
    if(md_pending_ && midi_ && sq_session_.GetPhase() == SqSession::Phase::Failed) {
        md_pending_ = false; // no SQ device: a Mod on Daisy firmware, as before
        sq_session_.Disconnect();
        session_.Connect(*midi_, now_);
    }
    if(boot_port_)
        for(const std::vector<uint8_t>& m : boot_port_->Take())
            boot_.OnMessage(m.data(), m.size());
    session_.Tick(now_);
    // SD card page: the CDC session and the workflow (SD_SHARE_PORT.md § 4). They run before
    // the port-release rule below, so the workflow attaches to a port the user just opened.
    ddp_session_.Tick(now_);
    sd_.Tick(now_);
    if(sd_.GetPhase() != last_sd_phase_) {
        const sd::Phase before = last_sd_phase_;
        last_sd_phase_ = sd_.GetPhase();
        OnSdPhase(before, last_sd_phase_);
    }
    // The session gave up (no answer to IDENTIFY, refused send, wrong protocol): the
    // port is released at once, so another application or a new attempt can open it,
    // unless the SD share workflow still talks to an instrument without a matrix on it (D4).
    if(session_.State() == LinkState::Offline && !md_pending_ && !SqActive() && !sd_link_
       && (midi_ || simulator_ || sq_sim_ || sw_sim_ || sd_sim_midi_))
        ReleasePort();
    FinishFirmwareUpdate(boot_.Poll(now_));
}

// Everything the MIDI input queued since the last call, to the conversation it belongs to.
void EditorComponent::RouteMidiFrames() {
    if(!midi_)
        return;
    for(const std::vector<uint8_t>& m : midi_->Take()) {
        if(sd::IsSdShareSysEx(m.data(), m.size())) { // Nebulae "NB" / Looper "LP": the SD share workflow
            if(sd_link_)
                sd_link_->Push(m.data(), m.size());
            continue;
        }
        boot_.OnMessage(m.data(), m.size());
        if(ClassifySysEx(m.data(), m.size()) == Protocol::Sq)
            sq_session_.OnMessage(m.data(), m.size(), now_);
        else
            session_.OnMessage(m.data(), m.size());
    }
}

bool EditorComponent::MatrixDetectionPending() const {
    if(md_pending_ || session_.State() == LinkState::Connecting)
        return true;
    if(sq_session_.GetPhase() == SqSession::Phase::Reading)
        return true;
    return sq_session_.Ready() && sq_session_.Identity().HasModMatrix() && !session_.Connected();
}

void EditorComponent::DisableHardwareSerial() {
    ddp_session_.Disconnect("");
    if(!sd_sim_serial_)
        ddp_session_.SetPorts(nullptr);
    serial_ports_.reset();
}

// ------------------------------------------------------------------ SD card page
bool EditorComponent::SdInstrumentFound() const {
    const sd::Phase p = sd_.GetPhase();
    return p != sd::Phase::NotFound && p != sd::Phase::Restarting && p != sd::Phase::Returning;
}

juce::String EditorComponent::CurrentPortLabel() const { return PortOpen() ? port_.getText() : juce::String(); }

// SD_SHARE_PORT.md § 7: the workflow probes the port the user connected to; while it follows
// a restart it caused (Restarting: R1 check, Returning) it may open a Daisy port itself.
std::vector<std::string> EditorComponent::SdPortCandidates() {
    if(!sd_page_available_)
        return {};
    if(PortOpen()) {
        const juce::String label = CurrentPortLabel();
        return label.isEmpty() ? std::vector<std::string>{} : std::vector<std::string>{label.toStdString()};
    }
    if(!sd_.Following() || user_disconnected_)
        return {};
    // Only the kit that left: the port with the identifier the share started on, or, for
    // ports without one (simulators), the single entry with that name. Two entries with the
    // same name or a vanished port mean the identity is unknown: no automatic connection,
    // the patch saved before the share stays in its file (D5, review finding 4).
    sd_open_identifier_.clear();
    const juce::String wanted = U(sd_.Port());
    int same_label = 0;
    for(const Port& p : ports_) {
        const bool candidate = sd_sim_ ? (p.simulator == sd_sim_port_ && sd_sim_->MidiAlive()) : (p.simulator == -1);
        if(!candidate || p.label != wanted)
            continue;
        if(sd_share_port_id_.isNotEmpty() && p.out.identifier == sd_share_port_id_) {
            sd_open_identifier_ = p.out.identifier;
            return {p.label.toStdString()};
        }
        ++same_label;
    }
    if(same_label == 1 && sd_share_port_id_.isEmpty())
        return {wanted.toStdString()};
    if(same_label > 1 && !sd_ambiguous_logged_) {
        sd_ambiguous_logged_ = true;
        Log(LogKind::Warn, U("Several MIDI ports are called “") + wanted + U("”; connect to the kit yourself. ")
                               + (sd_restore_ ? U("The patch saved before the share stays in ") + sd_autosave_file_.getFullPathName()
                                              : juce::String()));
    }
    return {};
}

bool EditorComponent::Busy() const {
    return session_.State() != LinkState::Offline || SqActive() || md_pending_ || sd_link_ != nullptr;
}

std::unique_ptr<sd::MidiLink> EditorComponent::OpenSdLink(const std::string& name, std::string& error) {
    if(!PortOpen() || CurrentPortLabel().toStdString() != name) {
        int index = -1, matches = 0;
        for(std::size_t i = 0; i < ports_.size(); ++i)
            if(ports_[i].label.toStdString() == name
               && (sd_open_identifier_.isEmpty() || ports_[i].out.identifier == sd_open_identifier_)) {
                index = int(i);
                ++matches;
            }
        if(index < 0 || matches != 1) {
            error = matches > 1 ? "several ports are called " + name : "port " + name + " is not listed";
            return nullptr;
        }
        opening_sd_link_ = true;
        ConnectTo(index); // SQ and Mod on Daisy detection as for a manual Connect
        opening_sd_link_ = false;
        sd_reconnected_ = true; // this connection is the kit coming back (restore allowed)
        if(!PortOpen()) {
            error = "port " + name + " could not be opened";
            return nullptr;
        }
    }
    auto link = std::make_unique<SdLink>(*this);
    sd_link_ = link.get();
    return link;
}

bool EditorComponent::SendSdSysEx(const uint8_t* bytes, std::size_t n, SdLink& link) {
    if(&link != sd_link_)
        return false;
    if(sd_sim_midi_)
        return sd_sim_midi_->Send(bytes, n);
    return midi_ && midi_->Send(bytes, n);
}

bool EditorComponent::SdPortAlive() const {
    if(sd_sim_midi_)
        return sd_sim_ && sd_sim_->MidiAlive();
    return midi_ != nullptr;
}

void EditorComponent::OnSdPhase(sd::Phase before, sd::Phase now) {
    if(now == sd::Phase::Restarting) {
        // SHARE accepted (T10): the kit restarts into SD management and its MIDI port
        // disappears. Release the matrix now, without a "connection lost" error.
        sd_restore_pending_ = sd_restore_.has_value();
        Disconnect();
        Log(LogKind::Info, U(sd_.Name()) + U(" restarts into SD management; the matrix connection is closed until it is back."));
    }
    if(now == sd::Phase::NotFound) {
        sd_auto_switched_ = false;
        sd_ambiguous_logged_ = false;
        if(sd_restore_pending_) { // the kit never came back (T22, T33): nothing to send it to
            sd_restore_pending_ = false;
            Log(LogKind::Warn, U("The kit did not come back; the patch saved before the share stays in ")
                                   + sd_autosave_file_.getFullPathName());
        }
    }
    if(before == sd::Phase::Returning && now == sd::Phase::Performance)
        Log(LogKind::Ok, U(sd_.Message()));
    RefreshAll();
}

bool EditorComponent::RequestSdShare(bool confirm) {
    if(!sd_.ManagementConnected() && MatrixDetectionPending()) {
        Log(LogKind::Warn, U("The matrix of this kit is still being read; wait a moment so the patch can be saved before "
                             "the share."));
        RefreshAll();
        return false;
    }
    // D5: the snapshot is taken right before the request that can restart the kit, also on
    // the confirmation (the user may have edited while the question was open).
    if(!sd_.ManagementConnected() && (sd_.GetPhase() == sd::Phase::Performance || (confirm && sd_.GetPhase() == sd::Phase::Confirm)))
        SaveBeforeShare();
    const bool ok = sd_.RequestShare(confirm);
    if(ok)
        Log(LogKind::Info, U(sd_.Name()) + (confirm ? U(": sharing confirmed; unsaved audio in RAM will be lost")
                                                      : U(": SD card sharing requested")));
    RefreshAll();
    return ok;
}

// D5: the matrix is volatile and the share restarts the kit, so the patch is written to a
// file first and kept to be sent back after the return.
bool EditorComponent::SaveBeforeShare() {
    sd_restore_.reset();
    sd_restore_pending_ = false;
    if(!session_.Connected()) {
        Log(LogKind::Info, U("No modulation matrix on this connection: nothing to save before the share."));
        return false;
    }
    const std::string text = SavePatch(model_, &session_.Device());
    sd_restore_ = LoadPatch(text, model_);
    sd_restore_key_ = session_.Device().key;
    sd_share_port_id_ = midi_ ? midi_->Output().identifier : juce::String();
    sd_ambiguous_logged_ = false;
    juce::File dir = sd_autosave_dir;
    if(dir == juce::File()) {
        const juce::String local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
        dir = (local.isNotEmpty() ? juce::File(local) : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory))
                  .getChildFile("Daisy Control Panel")
                  .getChildFile("autosave");
    }
    dir.createDirectory();
    const juce::String stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    sd_autosave_file_ = dir.getChildFile(juce::File::createLegalFileName(U(session_.Device().key)) + "-" + stamp + ".modpatch");
    const juce::File file = sd_autosave_file_;
    const juce::String content = U(text);
    if(sd_autosave_sync) {
        if(!file.replaceWithText(content))
            Log(LogKind::Error, U("Patch could not be saved to ") + file.getFullPathName() + U("; it is kept in memory and sent back after the return."));
        else
            Log(LogKind::Info, U("Patch saved to ") + file.getFullPathName() + U("; it is sent back after the return."));
        return true;
    }
    // The disk write runs on a worker (review finding 9); the in-memory copy is what the
    // restore uses, so a slow drive never delays the request.
    juce::Component::SafePointer<EditorComponent> safe(this);
    juce::Thread::launch([file, content, safe] {
        const bool ok = file.replaceWithText(content);
        juce::MessageManager::callAsync([file, ok, safe] {
            if(EditorComponent* self = safe.getComponent())
                self->Log(ok ? LogKind::Info : LogKind::Error,
                          ok ? U("Patch saved to ") + file.getFullPathName() + U("; it is sent back after the return.")
                             : U("Patch could not be saved to ") + file.getFullPathName() + U("; it is kept in memory and sent back after the return."));
        });
    });
    Log(LogKind::Info, U("Saving the patch to ") + file.getFullPathName() + U(" …"));
    return true;
}

// Only for the kit that comes back from the share the workflow caused: the same instrument
// key on a connection the workflow opened. Any other connection abandons the restore (the
// file stays). Cleared as well when the kit never comes back (T22, T33).
void EditorComponent::RestoreAfterShare() {
    if(!sd_restore_pending_ || !sd_restore_ || !session_.Connected())
        return;
    sd_restore_pending_ = false;
    if(!sd_reconnected_ || session_.Device().key != sd_restore_key_) {
        Log(LogKind::Warn, U("Patch saved before the share not restored: another device (") + U(session_.Device().key)
                               + U(" instead of ") + U(sd_restore_key_) + U("); open it from ") + sd_autosave_file_.getFullPathName());
        return;
    }
    const Compatibility compat = CheckCompatibility(*sd_restore_, model_, session_.Device());
    ApplyDecision(*sd_restore_, compat, compat.NeedsDecision() ? CompatChoice::Safe : CompatChoice::Force);
    Log(LogKind::Ok, U("Patch restored after the share (") + sd_autosave_file_.getFileName() + U(")."));
}

void EditorComponent::SdEjectHelp() {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, U("Eject the card in Windows"),
        U("1. Close every file and program that uses the ") + U(sd_.Name())
            + U(" drive.\n2. Use “Safely Remove Hardware” in the taskbar, or right-click the drive in Explorer and choose "
                "“Eject”.\n3. Wait until this page shows “Windows ejected the card”. Only the firmware's EJECTED state "
                "counts; this button does not eject anything."),
        U("OK"), this);
}

void EditorComponent::SdAcknowledge() {
    if(!sd_.CanAcknowledge())
        return;
    // The question is about this interruption on this CDC connection; if either changed
    // while it was open, nothing is sent (review finding 3).
    const int episode = sd_.InterruptionEpisode(), generation = ddp_session_.Generation();
    juce::Component::SafePointer<EditorComponent> safe(this);
    const std::function<void(bool)> apply = [safe, episode, generation](bool yes) {
        EditorComponent* self = safe.getComponent();
        if(!self)
            return;
        if(yes) {
            if(self->sd_.InterruptionEpisode() != episode || self->ddp_session_.Generation() != generation
               || !self->sd_.CanAcknowledge())
                self->Log(LogKind::Warn, U("The interrupted access changed while the question was open; nothing was "
                                           "acknowledged. Check the card reader state and acknowledge again."));
            else if(self->sd_.AcknowledgeInterruption())
                self->Log(LogKind::Info, U(self->sd_.Name()) + U(": interrupted access acknowledged"));
        }
        self->RefreshAll();
    };
    if(sd_ack_prompt) // self-test: answers later, after the state may have changed
        return sd_ack_prompt(apply);
    if(sd_ack_decision) // self-test: answers without a modal window
        return apply(sd_ack_decision());
    auto* w = new juce::AlertWindow(U("Interrupted SD access"),
                                    U("Windows may have lost pending changes. Close all files using this card. Acknowledging "
                                      "does not recover data and is not a clean eject. Continue?"),
                                    juce::MessageBoxIconType::WarningIcon, this);
    w->setLookAndFeel(&look_);
    w->addButton(U("Yes"), 1);
    w->addButton(U("No"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([apply](int result) { apply(result == 1); }), true);
}

// ------------------------------------------------------------------ firmware update
// The kit restarts into the Daisy bootloader (common/bootloader/README.md). Works with
// every firmware of this repository that has USB-MIDI, also without a Mod on Daisy
// connection: then the selected port is opened just for the request.
void EditorComponent::ConfirmFirmwareUpdate() {
    auto* w = new juce::AlertWindow(
        U("Switch the kit to update mode?"),
        U("The kit restarts into the Daisy bootloader: the program stops, the MIDI connection ends and all "
          "16 LEDs light up. Then load the new firmware with the Daisy Web Programmer or “make program-dfu”; "
          "the kit waits without a time limit."),
        juce::MessageBoxIconType::QuestionIcon, this);
    w->setLookAndFeel(&look_);
    w->addButton(U("Start update mode"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(U("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
                           if(result == 1)
                               StartFirmwareUpdate(port_.getSelectedItemIndex());
                       }),
                       true);
}

void EditorComponent::StartFirmwareUpdate(int index) {
    if(boot_.Waiting())
        return;
    const bool valid = index >= 0 && index < int(ports_.size());
    if(simulator_ || sq_sim_ || (!midi_ && valid && ports_[std::size_t(index)].simulator != -1))
        return Log(LogKind::Warn, U("A simulator has no bootloader."));
    MidiPort* port = midi_.get();
    if(!port) {
        if(!valid)
            return Log(LogKind::Warn, U("Please choose a device."));
        juce::String error;
        boot_port_ = MidiPort::Open(ports_[std::size_t(index)].out, ports_[std::size_t(index)].in, error);
        if(!boot_port_)
            return Log(LogKind::Error, error);
        port = boot_port_.get();
    }
    if(!boot_.Start(now_, [port](const uint8_t* b, std::size_t n) { return port->Send(b, n); })) {
        boot_port_.reset();
        return Log(LogKind::Error, U("The MIDI port refused the request."));
    }
    Log(LogKind::Info, U("Update mode requested …"));
    RefreshAll();
}

void EditorComponent::FinishFirmwareUpdate(BootRequest::Result result) {
    if(result == BootRequest::Result::None)
        return;
    boot_port_.reset();
    if(result == BootRequest::Result::Acknowledged) {
        Disconnect(); // the port disappears with the reset; no "connection lost" message
        // The reply confirms that the request was accepted, not the DFU entry itself.
        Log(LogKind::Ok, U("Request accepted: the kit restarts into update mode (all LEDs on; the Looper only after "
                           "running card work). As soon as it appears as a DFU device, load the firmware with "
                           "the Daisy Web Programmer or “make program-dfu”."));
    } else {
        Log(LogKind::Warn, U("No answer from the kit. Older firmware does not know the command: then hold S9 and S16 for five "
                             "seconds or use RESET and BOOT (docs/FLASHING.md)."));
    }
    RefreshAll();
}

void EditorComponent::ReleasePort() {
    grid_.ClearMonitor();
    simulator_.reset();
    sd_sim_midi_.reset(); // the simulated kit itself (sd_sim_) stays plugged in
    sq_link_.send = {};
    md_link_.send = {};
    sq_sim_.reset();
    sw_sim_.reset();
    sq_sim_out_.clear();
    midi_.reset();
    RefreshAll();
}

// ------------------------------------------------------------------ view
void EditorComponent::handleAsyncUpdate() { RefreshAll(); }

// The model that sounds (PROTOCOL.md § 3.6), with how to select it on the kit.
void EditorComponent::RefreshMode() {
    const DeviceInfo& d = session_.Device();
    juce::String text, tip;
    if(session_.Connected() && d.Modes()) {
        if(mode_index_ >= 0 && mode_index_ < int(d.modes.size()) && !d.modes[std::size_t(mode_index_)].name.empty()) {
            const DeviceInfo::Mode& m = d.modes[std::size_t(mode_index_)];
            text = U("Model: ") + U(m.name) + (mode_origin_ == 1 ? U(" (MIDI)") : U(""));
            tip = U(m.name) + U(" – on the device: ") + U(m.hint)
                  + (mode_origin_ == 1 ? U("\nLast selected over MIDI (CC28); the switches may be set differently.")
                                       : U(""));
        } else {
            text = U("Model: …");
        }
        tip << U("\nAll models:");
        for(std::size_t i = 0; i < d.modes.size(); ++i)
            tip << U("\n  ") << U(d.modes[i].name) << U(" – ") << U(d.modes[i].hint);
    }
    mode_.setText(text, juce::dontSendNotification);
    mode_.setTooltip(tip);
}

// The device has priority (D13): a song's patch is sent only after confirmation, or
// always once "Always send" was chosen for this plugin instance.
void EditorComponent::AskProjectUpload() {
    if(!sq_project_ || !sq_session_.Ready())
        return;
    const AcidPatch patch = std::move(*sq_project_);
    sq_project_.reset();
    sq_panel_.SetPendingPatch(patch);
    const AcidCompat compat = CheckAcidPatch(patch, sq_session_);
    const auto apply = [this](int choice) {
        if(choice == 2)
            sq_always_upload_ = true;
        if(choice >= 1) {
            const int n = sq_panel_.Upload();
            Log(LogKind::Ok, U("Patch from the project sent (") + juce::String(n) + U(" values)."));
        } else {
            Log(LogKind::Info, U("Patch from the project not sent; the device keeps its state."));
        }
    };
    if(sq_always_upload_)
        return apply(1);
    if(sq_upload_decision)
        return apply(sq_upload_decision(compat));
    juce::String text = U("The project contains a product patch. The device has priority: without confirmation it keeps its state.");
    if(!compat.Clean())
        text << U("\nParts that do not fit are skipped.");
    auto* w = new juce::AlertWindow(U("Send the patch from the project?"), text, juce::MessageBoxIconType::QuestionIcon, this);
    w->addButton(U("Send"), 1);
    w->addButton(U("Always send"), 2);
    w->addButton(U("Don't send"), 0);
    w->enterModalState(true, juce::ModalCallbackFunction::create(apply), true);
}

void EditorComponent::RefreshAll() {
    const bool sq_ready = sq_session_.Ready();
    if(sq_ready && !sq_was_ready_)
        AskProjectUpload(); // the device state has just been read
    sq_was_ready_ = sq_ready;
    const bool matrix_available = sq_ready && sq_session_.Identity().HasModMatrix();
    if(!matrix_available)
        show_matrix_ = false;
    view_.setVisible(matrix_available);
    view_.setEnabled(session_.Connected() || show_matrix_);
    view_.setButtonText(U(show_matrix_ ? "Sound parameters" : "Matrix"));
    RefreshPages();
    sq_panel_.setVisible(sq_ready && !show_matrix_ && !show_sd_);
    if(sq_ready && !show_matrix_) {
        sq_panel_.Refresh();
        status_.setText(U(sq_session_.Identity().product + " " + sq_session_.Identity().version + " · SQ" +
                          (matrix_available ? (session_.Connected() ? " + Matrix" : " · Matrix connecting …") : "")),
                        juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, colour::ok);
        connect_.setButtonText("Disconnect");
        port_.setEnabled(false);
        if(patch_changed) {
            std::string text = PatchText();
            if(text != last_patch_text_) {
                last_patch_text_ = std::move(text);
                patch_changed(last_patch_text_);
            }
        }
        return;
    }
    const bool connected = session_.Connected();
    if(connected && !was_connected_ && pending_project_) { // the device state has just been read
        const LoadedPatch project = std::move(*pending_project_);
        pending_project_.reset();
        SendProjectPatch(project);
    }
    if(connected && !was_connected_)
        RestoreAfterShare(); // D5: the patch saved before a share, after the kit is back
    was_connected_ = connected;
    if(patch_changed) {
        std::string text = PatchText();
        if(text != last_patch_text_) {
            last_patch_text_ = std::move(text);
            patch_changed(last_patch_text_);
        }
    }
    const LinkState s = session_.State();
    status_.setText(U(session_.StatusText()), juce::dontSendNotification);
    const Health health = session_.LinkHealth();
    status_.setColour(juce::Label::textColourId,
                      s == LinkState::Connected ? (health == Health::Ok ? colour::ok
                                                   : health == Health::NoLiveData ? colour::warn : colour::err)
                                                : (session_.StatusText() == "offline" ? colour::muted : colour::err));
    // An instrument the SD share workflow found that has no matrix (plain Nebulae, Looper):
    // the status says so instead of "error"; in SD management the MIDI side is gone anyway.
    if(s == LinkState::Offline && !md_pending_ && !SqActive() && SdInstrumentFound()) {
        const bool mgmt = sd_.GetPhase() == sd::Phase::Management;
        const juce::String fw = sd_.Status() ? U(" ") + U(sd_.Status()->firmware) : juce::String();
        status_.setText(U(sd_.GetProfile().display_name) + fw + (mgmt ? U(" · SD management (USB CDC)") : U(" · no matrix, SD card page")),
                        juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, mgmt ? colour::warn : colour::ok);
    }
    status_.setColour(juce::Label::outlineColourId, status_.findColour(juce::Label::textColourId));
    const bool busy = Busy();
    connect_.setButtonText(busy ? "Disconnect" : "Connect");
    RefreshMode();
    port_.setEnabled(!busy);
    update_.setEnabled(!boot_.Waiting() && !simulator_ && !sq_sim_ && !sw_sim_ && !sd_sim_midi_ && !ports_.empty());
    pull_.setEnabled(session_.Connected());
    count_.setText(U("Active connections: ") + juce::String(model_.ActiveCount()) + " / "
                       + juce::String(model_.max_active),
                   juce::dontSendNotification);
    for(auto& panel : sources_)
        panel->Refresh();
    if(!name_.hasKeyboardFocus(true))
        name_.setText(U(model_.Name()), juce::dontSendNotification);
    if(session_.Connected()) {
        const DeviceInfo& d = session_.Device();
        device_info_.setText(U(d.key) + " " + U(d.version) + ", Mod on Daisy " + juce::String(d.module_major) + "."
                                 + juce::String(d.module_minor) + "." + juce::String(d.module_patch) + ", protocol "
                                 + juce::String(d.protocol_major) + "." + juce::String(d.protocol_minor) + ", tick "
                                 + juce::String(d.tick_hz) + " Hz, schema " + U(Hex32(d.schema)) + U(".\nMemory slots on the device: ")
                                 + (d.slots ? juce::String(d.slots) : U("none yet (save patches as files)."))
                                 + (simulator_ ? U("\nSimulator: the firmware code on the PC, no Daisy needed.") : U("")),
                             juce::dontSendNotification);
    } else {
        device_info_.setText(U("Not connected. The editor works offline; changes can be saved as a file."),
                             juce::dontSendNotification);
    }
    RefreshCellEditor();
    grid_.repaint();
}

// The two pages. D4: an instrument the SD share workflow found that has no matrix opens
// on the SD card page (once per device), and the matrix page says so.
void EditorComponent::RefreshPages() {
    const bool mgmt = sd_.GetPhase() == sd::Phase::Management;
    const bool no_matrix = SdInstrumentFound() && !mgmt && session_.State() == LinkState::Offline && !md_pending_ && !SqActive();
    if(sd_page_available_ && (no_matrix || mgmt) && !sd_auto_switched_) {
        sd_auto_switched_ = true;
        if(!show_sd_) {
            show_sd_ = true;
            resized();
        }
    }
    page_matrix_.setToggleState(!show_sd_, juce::dontSendNotification);
    page_sd_.setToggleState(show_sd_, juce::dontSendNotification);
    sd_panel_.SetSimulated(sd_sim_ != nullptr);
    sd_panel_.setVisible(show_sd_);
    if(show_sd_)
        sd_panel_.Refresh();
    if(no_matrix) {
        const juce::String fw = sd_.Status() ? U(" ") + U(sd_.Status()->firmware) : juce::String();
        no_matrix_.setText(U(sd_.GetProfile().display_name) + fw + U(" has no modulation matrix. Use the SD card page."),
                           juce::dontSendNotification);
    }
    no_matrix_.setVisible(no_matrix && !show_sd_);
}

void EditorComponent::SetAbout(const juce::String& text) { sd_panel_.SetAbout(text); }

void EditorComponent::RefreshCellEditor() {
    const int c = model_.selected;
    const bool ok = model_.Allowed(c);
    const Column& col = model_.columns[std::size_t(TargetOf(c))];
    route_.setText(U(kSourceNames[SourceOf(c)]) + U(" → ") + (col.present ? U(col.name) : U("—")),
                   juce::dontSendNotification);
    depth_.setValue(model_.Depth(c), juce::dontSendNotification);
    enabled_.setToggleState(model_.Enabled(c), juce::dontSendNotification);
    for(juce::Component* comp : std::initializer_list<juce::Component*>{&depth_, &enabled_, &zero_, &invert_})
        comp->setEnabled(ok);
    juce::String info;
    if(!ok) {
        info = U(model_.BlockedReason(c));
    } else {
        info = U("Depth 100 = ") + (col.span_text.empty() ? U("full range") : U(col.SpanLabel()));
        if(SourceOf(c) == mod::PolyAt)
            info << U(col.per_voice ? "\nPoly AT per voice" : "\nPoly AT of the last played note");
        if(col.latched)
            info << U("\ntakes effect on the next note");
    }
    cell_info_.setText(info, juce::dontSendNotification);
}

void EditorComponent::Log(LogKind kind, const juce::String& text) {
    const juce::int64 now = juce::Time::currentTimeMillis();
    // A confirmation never hides a warning or error that is less than 4 s old.
    if(kind == LogKind::Ok && now - last_alert_ < 4000)
        return;
    if(kind == LogKind::Warn || kind == LogKind::Error)
        last_alert_ = now;
    log_.setText(text, juce::dontSendNotification);
    log_.setColour(juce::Label::textColourId, LogColour(kind));
}

void EditorComponent::paint(juce::Graphics& g) {
    g.fillAll(colour::bg);
    g.setColour(colour::panel);
    g.fillRect(header_area_);
    g.setColour(colour::line);
    g.drawHorizontalLine(header_area_.getBottom() - 1, 0.0f, float(getWidth()));
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    if(sd_page_available_) { // the standalone app (D3): "Daisy Control Panel"
        juce::Rectangle<int> title = header_area_.withTrimmedLeft(kPad).withWidth(200);
        g.setColour(colour::accent);
        g.drawText("Daisy", title, juce::Justification::centredLeft);
        g.setColour(colour::ink);
        g.drawText("Control Panel", title.withTrimmedLeft(58), juce::Justification::centredLeft);
    } else { // the VST3 keeps its name
        juce::Rectangle<int> title = header_area_.withTrimmedLeft(kPad).withWidth(170);
        g.setColour(colour::ink);
        g.drawText("Mod on", title, juce::Justification::centredLeft);
        g.setColour(colour::accent);
        g.drawText("Daisy", title.withTrimmedLeft(66), juce::Justification::centredLeft);
    }
    PaintCard(g, grid_card_, "Modulation matrix");
    PaintCard(g, cell_card_, "Connection");
    PaintCard(g, footer_card_, {});
}

void EditorComponent::resized() {
    juce::Rectangle<int> r = getLocalBounds();
    sq_panel_.setBounds(r.withTrimmedTop(kHeader));
    sd_panel_.setBounds(r.withTrimmedTop(kHeader));
    header_area_ = r.removeFromTop(kHeader);
    {
        juce::Rectangle<int> h = header_area_.reduced(kPad, 12).withTrimmedLeft(sd_page_available_ ? 200 : 170);
        port_.setBounds(h.removeFromLeft(260));
        h.removeFromLeft(8);
        connect_.setBounds(h.removeFromLeft(100));
        h.removeFromLeft(12);
        page_matrix_.setBounds(h.removeFromLeft(72));
        h.removeFromLeft(4);
        page_sd_.setBounds(h.removeFromLeft(72));
        update_.setBounds(h.removeFromRight(150));
        h.removeFromRight(8);
        view_.setBounds(h.removeFromRight(130));
        h.removeFromRight(12);
        h.removeFromLeft(12);
        status_.setBounds(h.removeFromLeft(300));
        h.removeFromLeft(12);
        mode_.setBounds(h);
    }
    r.reduce(kPad, kPad);
    footer_card_ = r.removeFromBottom(kFooter);
    r.removeFromBottom(kPad);
    juce::Rectangle<int> sources = r.removeFromBottom(kSourcesHeight);
    r.removeFromBottom(kPad);
    cell_card_ = r.removeFromRight(kCellCard);
    r.removeFromRight(kPad);
    grid_card_ = r;

    {
        juce::Rectangle<int> g = grid_card_.reduced(kPad, 10).withTrimmedTop(24);
        hint_.setBounds(g.removeFromBottom(20));
        count_.setBounds(g.removeFromBottom(24));
        grid_.setBounds(g);
        no_matrix_.setBounds(g.withSizeKeepingCentre(g.getWidth() - 40, 60));
    }
    {
        juce::Rectangle<int> c = cell_card_.reduced(kPad, 10).withTrimmedTop(26);
        route_.setBounds(c.removeFromTop(26));
        c.removeFromTop(6);
        depth_.setBounds(c.removeFromTop(30));
        c.removeFromTop(6);
        enabled_.setBounds(c.removeFromTop(26));
        c.removeFromTop(6);
        juce::Rectangle<int> buttons = c.removeFromTop(28);
        zero_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft(8);
        invert_.setBounds(buttons);
        c.removeFromTop(8);
        cell_info_.setBounds(c.removeFromTop(70));
        c.removeFromTop(12);
        device_heading_.setBounds(c.removeFromTop(20));
        device_info_.setBounds(c.removeFromTop(150));
    }
    {
        const int w = (sources.getWidth() - 2 * kPad) / 3;
        for(int i = 0; i < 3; ++i) {
            sources_[std::size_t(i)]->setBounds(i == 2 ? sources : sources.removeFromLeft(w));
            sources.removeFromLeft(kPad);
        }
    }
    {
        juce::Rectangle<int> f = footer_card_.reduced(kPad, 12);
        name_label_.setBounds(f.removeFromLeft(48));
        name_.setBounds(f.removeFromLeft(170));
        for(juce::TextButton* b : {&open_, &save_, &clear_, &pull_}) {
            f.removeFromLeft(8);
            b->setBounds(f.removeFromLeft(b == &clear_ ? 118 : 138));
        }
        f.removeFromLeft(14);
        log_.setBounds(f);
    }
}

// ------------------------------------------------------------------ files
void EditorComponent::OpenFileDialog() {
    chooser_ = std::make_unique<juce::FileChooser>(U("Open patch"), juce::File(), "*.modpatch;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              if(fc.getResult() != juce::File())
                                  LoadFile(fc.getResult());
                          });
}

void EditorComponent::SaveFileDialog() {
    const juce::String base = model_.Name().empty() ? juce::String("patch") : U(model_.Name());
    chooser_ = std::make_unique<juce::FileChooser>(
        U("Save patch"), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile(juce::File::createLegalFileName(base) + ".modpatch"),
        "*.modpatch");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
                              if(fc.getResult() != juce::File())
                                  SaveFile(fc.getResult().withFileExtension("modpatch"));
                          });
}

bool EditorComponent::SaveFile(const juce::File& file) {
    const std::string text = SavePatch(model_, session_.Connected() ? &session_.Device() : nullptr);
    if(!file.replaceWithText(U(text))) {
        Log(LogKind::Error, U("Cannot write the file: ") + file.getFullPathName());
        return false;
    }
    Log(LogKind::Ok, U("Saved: ") + file.getFileName());
    return true;
}

bool EditorComponent::LoadFile(const juce::File& file) {
    const LoadedPatch loaded = LoadPatch(file.loadFileAsString().toStdString(), model_);
    if(!loaded.ok) {
        Log(LogKind::Error, U(loaded.error));
        return false;
    }
    if(!session_.Connected()) { // offline: nothing to compare with; the file's origin is kept
        ApplyLoaded(loaded, loaded.patch);
        return true;
    }
    // Review finding F1: columns that mean something else on this device are not
    // transferred silently.
    const Compatibility compat = CheckCompatibility(loaded, model_, session_.Device());
    if(!compat.NeedsDecision()) {
        ApplyLoaded(loaded, loaded.patch);
        return true;
    }
    if(compat_decision) { // self-test: answers without a modal window
        ApplyDecision(loaded, compat, compat_decision(compat));
        return false;
    }
    juce::String text;
    for(const std::string& note : compat.notes)
        text << U("• ") << U(note) << "\n";
    text << U("\n“Load without affected” switches the affected connections off (their depth is kept) and "
              "loads everything else.");
    auto* w = new juce::AlertWindow(U("The patch does not exactly fit this device"), text,
                                    juce::MessageBoxIconType::WarningIcon, this);
    w->setLookAndFeel(&look_);
    w->addButton(U("Load without affected"), int(CompatChoice::Safe), juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(U("Load everything anyway"), int(CompatChoice::Force));
    w->addButton(U("Cancel"), int(CompatChoice::Cancel), juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, loaded, compat](int result) {
                           ApplyDecision(loaded, compat, CompatChoice(result));
                       }),
                       true);
    return false;
}

void EditorComponent::ApplyDecision(const LoadedPatch& loaded, const Compatibility& compat, CompatChoice choice) {
    if(choice == CompatChoice::Cancel) {
        Log(LogKind::Info, U("Loading cancelled; the device is unchanged."));
        return;
    }
    if(choice == CompatChoice::Safe) {
        int off = 0;
        for(int c = 0; c < kCells; ++c)
            off += (compat.affected & Bit(c)) ? 1 : 0;
        Log(LogKind::Warn, U("Loaded without ") + juce::String(off) + U(" affected connection(s)."));
        ApplyLoaded(loaded, SafePart(loaded.patch, compat));
    } else {
        ApplyLoaded(loaded, loaded.patch);
    }
}

void EditorComponent::ApplyLoaded(const LoadedPatch& loaded, const mod::Patch& patch) {
    session_.Upload(patch);
    model_.origin = session_.Connected() ? FileOrigin{} : loaded.Origin();
    RefreshAll();
}
}
