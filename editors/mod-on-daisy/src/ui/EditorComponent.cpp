#include "EditorComponent.h"

namespace editor::ui {
namespace {
constexpr int kHeader = 56, kCellCard = 300, kSourcesHeight = 250, kFooter = 56, kPad = 14;

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

EditorComponent::EditorComponent(bool with_simulators)
    : session_(model_, uint32_t(juce::Random::getSystemRandom().nextInt())), grid_(model_, session_),
      with_simulators_(with_simulators) {
    setLookAndFeel(&look_);
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

    addAndMakeVisible(port_);
    port_.setTooltip(U("Gerät: MIDI-Port des Daisy („Daisy Seed External“)"));
    addAndMakeVisible(connect_);
    connect_.setColour(juce::TextButton::buttonColourId, colour::accent);
    connect_.onClick = [this] {
        if(session_.State() != LinkState::Offline) {
            user_disconnected_ = true; // the plugin stays offline until "Verbinden"
            Disconnect();
        } else {
            user_disconnected_ = false;
            ConnectTo(port_.getSelectedItemIndex());
        }
    };
    addAndMakeVisible(update_);
    update_.setButtonText(U("Firmware-Update …"));
    update_.setTooltip(U("Startet das Kit im Daisy-Bootloader neu, ohne BOOT und RESET. Danach die Firmware mit "
                         "dem Daisy Web Programmer oder „make program-dfu“ laden."));
    update_.onClick = [this] { ConfirmFirmwareUpdate(); };
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
    hint_.setText(U("Zelle wählen · senkrecht ziehen ändert die Tiefe (Shift fein) · Mausrad ±1 (Shift ±10) · "
                    "Doppelklick ein/aus · Rechtsklick Menü · Pfeile, +/−, Bild↑/↓ ±10, 0, Leertaste"),
                  juce::dontSendNotification);

    for(juce::Label* l : {&route_, &cell_info_, &device_heading_, &device_info_})
        addAndMakeVisible(l);
    route_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    cell_info_.setColour(juce::Label::textColourId, colour::muted);
    cell_info_.setJustificationType(juce::Justification::topLeft);
    device_heading_.setText(U("GERÄT"), juce::dontSendNotification);
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
    open_.setButtonText(U("Datei öffnen …"));
    save_.setButtonText(U("Datei speichern …"));
    pull_.setButtonText(U("Vom Gerät laden"));
    for(juce::TextButton* b : {&open_, &save_, &clear_, &pull_})
        addAndMakeVisible(b);
    open_.onClick = [this] { OpenFileDialog(); };
    save_.onClick = [this] { SaveFileDialog(); };
    clear_.onClick = [this] { session_.ClearMatrix(); };
    pull_.setTooltip(U("Liest Matrix, Quellen und Name neu vom Gerät, z. B. nach Änderungen mit einem anderen "
                       "Programm. Ohne solche Änderungen bleibt alles gleich."));
    pull_.onClick = [this] {
        session_.Pull(false, [this] {
            Log(LogKind::Ok, U("Zustand vom Gerät geladen (Patch-CRC ") + U(Hex32(model_.Crc())) + ").");
        });
    };
    addAndMakeVisible(log_);
    log_.setFont(juce::FontOptions(13.0f));

    RefreshPorts();
    RefreshAll();
    Log(LogKind::Info, with_simulators_ ? U("Nicht verbunden. Ohne Gerät lässt sich offline bearbeiten oder ein Simulator wählen.")
                                        : U("Nicht verbunden. Ohne Gerät lässt sich offline bearbeiten."));
    setSize(1220, 920);
    startTimer(10);
}

EditorComponent::~EditorComponent() {
    stopTimer();
    session_.Disconnect("");
    setLookAndFeel(nullptr);
}

// ------------------------------------------------------------------ ports and connection
void EditorComponent::RefreshPorts() {
    const auto outs = juce::MidiOutput::getAvailableDevices();
    const auto ins = juce::MidiInput::getAvailableDevices();
    std::vector<Port> ports;
    for(int i = 0; with_simulators_ && i < int(std::size(kSimInstruments)); ++i)
        ports.push_back({U(kSimInstruments[i].label), i, {}, {}});
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
            Disconnect();
            session_.Disconnect(U("Gerät getrennt").toStdString());
            Log(LogKind::Error, U("Die Verbindung zum Daisy ist abgerissen. Bearbeitung geht offline weiter."));
        }
    }
    if(same)
        return;
    const juce::String previous = port_.getText();
    // While offline, a Daisy that appears later replaces a non-Daisy suggestion (the kit is
    // often plugged in or leaves the bootloader after the editor started).
    const bool keep = midi_ || simulator_ || previous.containsIgnoreCase("daisy");
    ports_ = std::move(ports);
    port_.clear(juce::dontSendNotification);
    int same_label = -1, daisy = -1;
    for(std::size_t i = 0; i < ports_.size(); ++i) {
        port_.addItem(ports_[i].label, int(i) + 1);
        if(same_label < 0 && previous.isNotEmpty() && ports_[i].label == previous)
            same_label = int(i);
        if(daisy < 0 && ports_[i].simulator < 0 && ports_[i].label.containsIgnoreCase("daisy"))
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
    if(index < 0 || index >= int(ports_.size()))
        return Log(LogKind::Warn, U("Bitte ein Gerät wählen."));
    const Port& p = ports_[std::size_t(index)];
    if(p.simulator >= 0) {
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
        session_.Connect(*midi_, now_);
    }
    port_.setSelectedItemIndex(index, juce::dontSendNotification);
    last_alert_ = 0; // messages of the previous device are over
    Log(LogKind::Info, U("Verbinde mit ") + p.label + U(" …"));
    RefreshAll();
}

void EditorComponent::Disconnect() {
    session_.Disconnect("");
    ReleasePort();
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
        if(ports_[i].simulator < 0 && ports_[i].label.containsIgnoreCase("daisy")) {
            port_.setSelectedItemIndex(int(i), juce::dontSendNotification);
            ConnectTo(int(i));
            return;
        }
}

std::string EditorComponent::PatchText() const {
    return SavePatch(model_, session_.Connected() ? &session_.Device() : nullptr);
}

void EditorComponent::LoadProjectPatch(const std::string& text) {
    const LoadedPatch loaded = LoadPatch(text, model_);
    if(!loaded.ok) {
        Log(LogKind::Error, U("Patch im Projekt nicht lesbar: ") + U(loaded.error));
        return;
    }
    if(session_.Connected()) {
        SendProjectPatch(loaded);
        return;
    }
    ApplyLoaded(loaded, loaded.patch); // shown offline at once
    pending_project_ = loaded;
    Log(LogKind::Info, U("Patch aus dem Projekt geladen; er geht beim Verbinden zum Gerät."));
}

// The project wins over what the device holds; mismatching columns stay off (F1, "safe").
void EditorComponent::SendProjectPatch(const LoadedPatch& loaded) {
    const Compatibility compat = CheckCompatibility(loaded, model_, session_.Device());
    ApplyDecision(loaded, compat, compat.NeedsDecision() ? CompatChoice::Safe : CompatChoice::Force);
    Log(LogKind::Ok, U("Patch aus dem Projekt zum Gerät gesendet."));
}

void EditorComponent::Advance(uint32_t now_ms) {
    now_ = now_ms;
    if(simulator_)
        simulator_->Advance(1, [this](const uint8_t* b, std::size_t n) { session_.OnMessage(b, n); });
    if(midi_)
        for(const std::vector<uint8_t>& m : midi_->Take()) {
            boot_.OnMessage(m.data(), m.size());
            session_.OnMessage(m.data(), m.size());
        }
    if(boot_port_)
        for(const std::vector<uint8_t>& m : boot_port_->Take())
            boot_.OnMessage(m.data(), m.size());
    session_.Tick(now_);
    // The session gave up (no answer to IDENTIFY, refused send, wrong protocol): the
    // port is released at once, so another application or a new attempt can open it.
    if(session_.State() == LinkState::Offline && (midi_ || simulator_))
        ReleasePort();
    FinishFirmwareUpdate(boot_.Poll(now_));
}

// ------------------------------------------------------------------ firmware update
// The kit restarts into the Daisy bootloader (common/bootloader/README.md). Works with
// every firmware of this repository that has USB-MIDI, also without a Mod on Daisy
// connection: then the selected port is opened just for the request.
void EditorComponent::ConfirmFirmwareUpdate() {
    auto* w = new juce::AlertWindow(
        U("Kit in den Update-Modus schalten?"),
        U("Das Kit startet in den Daisy-Bootloader neu: Das Programm hält an, die MIDI-Verbindung endet und alle "
          "16 LEDs leuchten. Danach die neue Firmware mit dem Daisy Web Programmer oder „make program-dfu“ "
          "laden; das Kit wartet ohne Zeitlimit."),
        juce::MessageBoxIconType::QuestionIcon, this);
    w->setLookAndFeel(&look_);
    w->addButton(U("Update-Modus starten"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(U("Abbrechen"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
    if(simulator_ || (!midi_ && valid && ports_[std::size_t(index)].simulator >= 0))
        return Log(LogKind::Warn, U("Ein Simulator hat keinen Bootloader."));
    MidiPort* port = midi_.get();
    if(!port) {
        if(!valid)
            return Log(LogKind::Warn, U("Bitte ein Gerät wählen."));
        juce::String error;
        boot_port_ = MidiPort::Open(ports_[std::size_t(index)].out, ports_[std::size_t(index)].in, error);
        if(!boot_port_)
            return Log(LogKind::Error, error);
        port = boot_port_.get();
    }
    if(!boot_.Start(now_, [port](const uint8_t* b, std::size_t n) { return port->Send(b, n); })) {
        boot_port_.reset();
        return Log(LogKind::Error, U("Der MIDI-Port hat die Anfrage abgelehnt."));
    }
    Log(LogKind::Info, U("Update-Modus angefordert …"));
    RefreshAll();
}

void EditorComponent::FinishFirmwareUpdate(BootRequest::Result result) {
    if(result == BootRequest::Result::None)
        return;
    boot_port_.reset();
    if(result == BootRequest::Result::Acknowledged) {
        Disconnect(); // the port disappears with the reset; no "connection lost" message
        // The reply confirms that the request was accepted, not the DFU entry itself.
        Log(LogKind::Ok, U("Anfrage angenommen: Das Kit startet in den Update-Modus (alle LEDs an, der Looper "
                           "erst nach laufender Kartenarbeit). Sobald es als DFU-Gerät erscheint, Firmware mit "
                           "dem Daisy Web Programmer oder „make program-dfu“ laden."));
    } else {
        Log(LogKind::Warn, U("Keine Antwort vom Kit. Ältere Firmware kennt den Befehl nicht: dann S9 und S16 fünf "
                             "Sekunden halten oder RESET und BOOT verwenden (docs/FLASHING.md)."));
    }
    RefreshAll();
}

void EditorComponent::ReleasePort() {
    grid_.ClearMonitor();
    simulator_.reset();
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
            text = U("Modell: ") + U(m.name) + (mode_origin_ == 1 ? U(" (MIDI)") : U(""));
            tip = U(m.name) + U(" – am Gerät: ") + U(m.hint)
                  + (mode_origin_ == 1 ? U("\nZuletzt per MIDI (CC28) gewählt; die Schalter stehen evtl. anders.")
                                       : U(""));
        } else {
            text = U("Modell: …");
        }
        tip << U("\nAlle Modelle:");
        for(std::size_t i = 0; i < d.modes.size(); ++i)
            tip << U("\n  ") << U(d.modes[i].name) << U(" – ") << U(d.modes[i].hint);
    }
    mode_.setText(text, juce::dontSendNotification);
    mode_.setTooltip(tip);
}

void EditorComponent::RefreshAll() {
    const bool connected = session_.Connected();
    if(connected && !was_connected_ && pending_project_) { // the device state has just been read
        const LoadedPatch project = std::move(*pending_project_);
        pending_project_.reset();
        SendProjectPatch(project);
    }
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
    status_.setColour(juce::Label::outlineColourId, status_.findColour(juce::Label::textColourId));
    connect_.setButtonText(s == LinkState::Offline ? "Verbinden" : "Trennen");
    RefreshMode();
    port_.setEnabled(s == LinkState::Offline);
    update_.setEnabled(!boot_.Waiting() && !simulator_ && !ports_.empty());
    pull_.setEnabled(session_.Connected());
    count_.setText(U("Aktive Verbindungen: ") + juce::String(model_.ActiveCount()) + " / "
                       + juce::String(model_.max_active),
                   juce::dontSendNotification);
    for(auto& panel : sources_)
        panel->Refresh();
    if(!name_.hasKeyboardFocus(true))
        name_.setText(U(model_.Name()), juce::dontSendNotification);
    if(session_.Connected()) {
        const DeviceInfo& d = session_.Device();
        device_info_.setText(U(d.key) + " " + U(d.version) + ", Mod on Daisy " + juce::String(d.module_major) + "."
                                 + juce::String(d.module_minor) + "." + juce::String(d.module_patch) + ", Protokoll "
                                 + juce::String(d.protocol_major) + "." + juce::String(d.protocol_minor) + ", Tick "
                                 + juce::String(d.tick_hz) + " Hz, Schema " + U(Hex32(d.schema)) + U(".\nSpeicherplätze im Gerät: ")
                                 + (d.slots ? juce::String(d.slots) : U("noch keine (Patches als Datei sichern)."))
                                 + (simulator_ ? U("\nSimulator: der Firmware-Code auf dem PC, kein Daisy nötig.") : U("")),
                             juce::dontSendNotification);
    } else {
        device_info_.setText(U("Nicht verbunden. Der Editor arbeitet offline; Änderungen lassen sich als Datei speichern."),
                             juce::dontSendNotification);
    }
    RefreshCellEditor();
    grid_.repaint();
}

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
        info = U("Tiefe 100 = ") + (col.span_text.empty() ? U("voller Bereich") : U(col.SpanLabel()));
        if(SourceOf(c) == mod::PolyAt)
            info << U(col.per_voice ? "\nPoly AT pro Stimme" : "\nPoly AT der zuletzt gespielten Note");
        if(col.latched)
            info << U("\nwirkt beim nächsten Anschlag");
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
    juce::Rectangle<int> title = header_area_.withTrimmedLeft(kPad).withWidth(170);
    g.setColour(colour::ink);
    g.drawText("Mod on", title, juce::Justification::centredLeft);
    g.setColour(colour::accent);
    g.drawText("Daisy", title.withTrimmedLeft(66), juce::Justification::centredLeft);
    PaintCard(g, grid_card_, "Modulationsmatrix");
    PaintCard(g, cell_card_, "Verbindung");
    PaintCard(g, footer_card_, {});
}

void EditorComponent::resized() {
    juce::Rectangle<int> r = getLocalBounds();
    header_area_ = r.removeFromTop(kHeader);
    {
        juce::Rectangle<int> h = header_area_.reduced(kPad, 12).withTrimmedLeft(170);
        port_.setBounds(h.removeFromLeft(300));
        h.removeFromLeft(8);
        connect_.setBounds(h.removeFromLeft(110));
        update_.setBounds(h.removeFromRight(150));
        h.removeFromRight(12);
        h.removeFromLeft(12);
        status_.setBounds(h.removeFromLeft(330));
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
    chooser_ = std::make_unique<juce::FileChooser>(U("Patch öffnen"), juce::File(), "*.modpatch;*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              if(fc.getResult() != juce::File())
                                  LoadFile(fc.getResult());
                          });
}

void EditorComponent::SaveFileDialog() {
    const juce::String base = model_.Name().empty() ? juce::String("patch") : U(model_.Name());
    chooser_ = std::make_unique<juce::FileChooser>(
        U("Patch speichern"), juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
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
        Log(LogKind::Error, U("Datei lässt sich nicht schreiben: ") + file.getFullPathName());
        return false;
    }
    Log(LogKind::Ok, U("Gespeichert: ") + file.getFileName());
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
    text << U("\n„Ohne betroffene laden“ schaltet die betroffenen Verbindungen aus (Tiefe bleibt erhalten) und "
              "lädt alles andere.");
    auto* w = new juce::AlertWindow(U("Patch passt nicht genau zu diesem Gerät"), text,
                                    juce::MessageBoxIconType::WarningIcon, this);
    w->setLookAndFeel(&look_);
    w->addButton(U("Ohne betroffene laden"), int(CompatChoice::Safe), juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(U("Trotzdem vollständig"), int(CompatChoice::Force));
    w->addButton(U("Abbrechen"), int(CompatChoice::Cancel), juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, loaded, compat](int result) {
                           ApplyDecision(loaded, compat, CompatChoice(result));
                       }),
                       true);
    return false;
}

void EditorComponent::ApplyDecision(const LoadedPatch& loaded, const Compatibility& compat, CompatChoice choice) {
    if(choice == CompatChoice::Cancel) {
        Log(LogKind::Info, U("Laden abgebrochen, das Gerät bleibt unverändert."));
        return;
    }
    if(choice == CompatChoice::Safe) {
        int off = 0;
        for(int c = 0; c < kCells; ++c)
            off += (compat.affected & Bit(c)) ? 1 : 0;
        Log(LogKind::Warn, U("Geladen ohne ") + juce::String(off) + U(" betroffene Verbindung(en)."));
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
