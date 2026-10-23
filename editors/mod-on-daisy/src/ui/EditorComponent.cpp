#include "EditorComponent.h"

namespace editor::ui {
namespace {
constexpr int kHeader = 56, kCellCard = 300, kSourcesHeight = 214, kFooter = 56, kPad = 14;

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

EditorComponent::EditorComponent()
    : session_(model_, uint32_t(juce::Random::getSystemRandom().nextInt())), grid_(model_, session_) {
    setLookAndFeel(&look_);
    session_.changed = [this] { triggerAsyncUpdate(); };
    session_.log = [this](LogKind k, const std::string& text) { Log(k, U(text)); };
    session_.monitor = [this](const MonitorValues& m) { grid_.SetMonitor(m); };

    addAndMakeVisible(port_);
    port_.setTooltip(U("Gerät: Simulator oder MIDI-Port des Daisy („Daisy Seed External“)"));
    addAndMakeVisible(connect_);
    connect_.setColour(juce::TextButton::buttonColourId, colour::accent);
    connect_.onClick = [this] {
        if(session_.State() != LinkState::Offline)
            Disconnect();
        else
            ConnectTo(port_.getSelectedItemIndex());
    };
    addAndMakeVisible(status_);
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
    pull_.onClick = [this] { session_.Pull(); };
    addAndMakeVisible(log_);
    log_.setFont(juce::FontOptions(13.0f));

    RefreshPorts();
    RefreshAll();
    Log(LogKind::Info, U("Nicht verbunden. Ohne Gerät lässt sich offline bearbeiten oder ein Simulator wählen."));
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
    for(int i = 0; i < int(std::size(kSimInstruments)); ++i)
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
    if(midi_) { // the open port vanished: unplugged
        bool present = false;
        for(const auto& out : outs)
            present = present || out.identifier == midi_->Output().identifier;
        if(!present) {
            Disconnect();
            session_.Disconnect(U("Gerät getrennt").toStdString());
            Log(LogKind::Error, U("Die Verbindung zum Daisy ist abgerissen. Bearbeitung geht offline weiter."));
        }
    }
    if(same)
        return;
    const juce::String previous = port_.getText();
    ports_ = std::move(ports);
    port_.clear(juce::dontSendNotification);
    int choose = 0;
    for(std::size_t i = 0; i < ports_.size(); ++i) {
        port_.addItem(ports_[i].label, int(i) + 1);
        if(ports_[i].label == previous)
            choose = int(i);
        else if(previous.isEmpty() && ports_[i].simulator < 0 && ports_[i].label.containsIgnoreCase("daisy"))
            choose = int(i); // a connected Daisy is the first suggestion
    }
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
    grid_.ClearMonitor();
    simulator_.reset();
    midi_.reset();
    RefreshAll();
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
        if(!port_.isPopupActive())
            RefreshPorts();
    }
}

void EditorComponent::Advance(uint32_t now_ms) {
    now_ = now_ms;
    if(simulator_)
        simulator_->Advance(1, [this](const uint8_t* b, std::size_t n) { session_.OnMessage(b, n); });
    if(midi_)
        for(const std::vector<uint8_t>& m : midi_->Take())
            session_.OnMessage(m.data(), m.size());
    session_.Tick(now_);
}

// ------------------------------------------------------------------ view
void EditorComponent::handleAsyncUpdate() { RefreshAll(); }

void EditorComponent::RefreshAll() {
    const LinkState s = session_.State();
    status_.setText(U(session_.StatusText()), juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId,
                      s == LinkState::Connected ? colour::ok
                                                : (session_.StatusText() == "offline" ? colour::muted : colour::err));
    status_.setColour(juce::Label::outlineColourId, status_.findColour(juce::Label::textColourId));
    connect_.setButtonText(s == LinkState::Offline ? "Verbinden" : "Trennen");
    port_.setEnabled(s == LinkState::Offline);
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
        h.removeFromLeft(12);
        status_.setBounds(h.removeFromLeft(360));
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
    const std::string& device = session_.Device().key;
    if(session_.Connected() && !loaded.instrument_key.empty() && loaded.instrument_key != device) {
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, U("Anderes Instrument"),
            U("Die Datei ist für „") + U(loaded.instrument_key) + U("“, verbunden ist „") + U(device)
                + U("“. Trotzdem laden?"),
            "Laden", "Abbrechen", this, juce::ModalCallbackFunction::create([this, loaded](int result) {
                if(result == 1)
                    ApplyLoaded(loaded);
            }));
        return false;
    }
    ApplyLoaded(loaded);
    return true;
}

void EditorComponent::ApplyLoaded(const LoadedPatch& loaded) {
    session_.Upload(loaded.patch);
    RefreshAll();
}
}
