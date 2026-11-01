#pragma once
// SQ pages of the JUCE editor and the VST3 (Acid Seq Phase 5, Streichwurst Phase 4): status
// and transport, 16-step pattern grid with note names and play position, step editor,
// parameter pages from the device's descriptors (Acid Seq: Sequencer, Voice, Drive,
// Delay, MIDI; Streichwurst: Strings, Solo, Keyboard, FX, Global by the contract's pages),
// calibration with the separate CV enable, product patch save/load/upload, counters.
// Streichwurst patches (.swpatch) embed the Mod on Daisy matrix through the two hooks.
// Everything goes through SqSession; the device is read first and written only on an
// explicit action (D13).
#include "../core/AcidPatch.h"
#include "../core/SqLabels.h"
#include "Theme.h"
#include <functional>
#include <memory>
#include <vector>

namespace editor::ui {

class SqPanel : public juce::Component {
  public:
    // Streichwurst: the matrix of the MD session as .modpatch text (empty: none) for
    // "Patch speichern", and its explicit upload with the product patch.
    std::function<std::string()> matrix_text;
    std::function<void(const std::string&)> upload_matrix;

    explicit SqPanel(SqSession& session) : s_(session) {
        for(juce::TextButton* b : {&start_, &stop_, &pull_, &save_, &load_, &upload_, &stats_btn_, &cv_on_, &cv_off_,
                                   &cal_send_[0], &cal_send_[1]})
            addAndMakeVisible(b);
        start_.onClick = [this] { s_.StartStop(true); };
        stop_.onClick = [this] { s_.StartStop(false); };
        pull_.onClick = [this] { s_.Pull(); };
        stats_btn_.onClick = [this] { s_.ReadStats([this](bool, uint8_t) { RefreshStats(); }); };
        cv_on_.onClick = [this] { s_.CvEnable(true); };
        cv_off_.onClick = [this] { s_.CvEnable(false); };
        save_.onClick = [this] { SaveDialog(); };
        load_.onClick = [this] { LoadDialog(); };
        upload_.onClick = [this] { Upload(); };
        upload_.setEnabled(false);
        for(juce::Label* l : {&status_, &patch_info_, &stats_, &cal_hint_, &step_label_})
            addAndMakeVisible(l);
        status_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        patch_info_.setColour(juce::Label::textColourId, colour::muted);
        stats_.setJustificationType(juce::Justification::topLeft);
        stats_.setFont(juce::FontOptions(12.0f));
        cal_hint_.setColour(juce::Label::textColourId, colour::muted);
        cal_hint_.setFont(juce::FontOptions(12.0f));
        for(int i = 0; i < 16; ++i) {
            auto b = std::make_unique<juce::TextButton>();
            b->setClickingTogglesState(false);
            b->onClick = [this, i] {
                selected_ = i;
                Refresh();
            };
            addAndMakeVisible(*b);
            steps_.push_back(std::move(b));
        }
        for(int n = 0; n < 60; ++n)
            note_.addItem(U(SqNoteName(n)), n + 1);
        note_.onChange = [this] { EditStep([this](step_seq::Step& st) { st.note = uint8_t(note_.getSelectedId() - 1); }); };
        gate_len_.setRange(5, 100, 1);
        gate_len_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
        gate_len_.onDragEnd = [this] { EditStep([this](step_seq::Step& st) { st.gate_percent = uint8_t(gate_len_.getValue()); }); };
        gate_.onClick = [this] { EditStep([this](step_seq::Step& st) { st.gate = gate_.getToggleState(); }); };
        accent_.onClick = [this] { EditStep([this](step_seq::Step& st) { st.accent = accent_.getToggleState(); }); };
        slide_.onClick = [this] { EditStep([this](step_seq::Step& st) { st.slide = slide_.getToggleState(); }); };
        for(juce::Component* c : std::initializer_list<juce::Component*>{&note_, &gate_len_, &gate_, &accent_, &slide_})
            addAndMakeVisible(c);
        for(int t = 0; t < 5; ++t) {
            tabs_[t].setButtonText(U(kAcidTabs[t].name));
            tabs_[t].setClickingTogglesState(false);
            tabs_[t].onClick = [this, t] {
                tab_ = t;
                rebuild_ = true;
                Refresh();
            };
            addAndMakeVisible(tabs_[t]);
        }
        static const char* fields[] = {"Code at 0 V", "Codes per volt", "Gate low", "Gate high", "Clamp min", "Clamp max"};
        for(int o = 0; o < 2; ++o) {
            for(int f = 0; f < 6; ++f) {
                cal_label_[o][f].setText(U(fields[f]), juce::dontSendNotification);
                addAndMakeVisible(cal_label_[o][f]);
                addAndMakeVisible(cal_edit_[o][f]);
            }
            cal_send_[o].onClick = [this, o] { SendCal(o); };
        }
        addAndMakeVisible(viewport_);
        viewport_.setViewedComponent(&param_box_, false);
        viewport_.setScrollBarsShown(true, false);
    }

    // Called by the editor on every session change.
    void Refresh() {
        if(!s_.Ready())
            return;
        const SqState& st = s_.State();
        const bool seq = s_.HasComponent("step-seq");
        const bool sw = Streichwurst();
        juce::String text = U(s_.Identity().product) + " " + U(s_.Identity().version);
        if(sw) {
            for(const SqComponent& c : s_.Components())
                if(c.kind != 4)
                    text << U(" · ") << U(c.id) << " " << U(c.version);
            text << U(" · Revision ") << int(st.applied) << "/" << int(st.accepted);
        }
        if(seq)
            text << U(" · ") << U(SqStateName(st.state)) << U(" · SW17 ") << (st.sw17 ? "Run" : "Stop") << U(" · ")
                 << U(st.mode == 0 ? "Forward" : st.mode == 1 ? "Reverse" : "Random") << U(" · ")
                 << juce::String(st.bpm, 1) << U(" BPM · Revision ") << int(st.applied) << "/" << int(st.accepted)
                 << U(st.cv_enabled ? " · CV enabled" : " · CV locked");
        status_.setText(text, juce::dontSendNotification);
        start_.setEnabled(seq);
        stop_.setEnabled(seq);
        start_.setVisible(seq); // transport exists only with a sequencer
        stop_.setVisible(seq);
        for(int i = 0; i < 16; ++i) {
            const auto& p = s_.Pattern()[std::size_t(i)];
            steps_[std::size_t(i)]->setButtonText((i < 8 ? "A" : "B") + juce::String(i % 8 + 1) + "\n" + U(SqNoteName(p.note)) +
                                                  (p.accent ? " A" : "") + (p.slide ? " S" : ""));
            juce::Colour c = p.gate ? colour::panel : colour::bg;
            if(i == st.step)
                c = colour::accent.withAlpha(0.35f);
            steps_[std::size_t(i)]->setColour(juce::TextButton::buttonColourId, c);
            steps_[std::size_t(i)]->setColour(juce::TextButton::textColourOffId, i == selected_ ? colour::accent : colour::ink);
            steps_[std::size_t(i)]->setVisible(seq);
        }
        const auto& sel = s_.Pattern()[std::size_t(selected_)];
        step_label_.setText(U("Step ") + (selected_ < 8 ? "A" : "B") + juce::String(selected_ % 8 + 1), juce::dontSendNotification);
        note_.setSelectedId(sel.note + 1, juce::dontSendNotification);
        if(!gate_len_.isMouseButtonDown())
            gate_len_.setValue(sel.gate_percent, juce::dontSendNotification);
        gate_.setToggleState(sel.gate, juce::dontSendNotification);
        accent_.setToggleState(sel.accent, juce::dontSendNotification);
        slide_.setToggleState(sel.slide, juce::dontSendNotification);
        for(juce::Component* c : std::initializer_list<juce::Component*>{&note_, &gate_len_, &gate_, &accent_, &slide_, &step_label_})
            c->setVisible(seq);
        const bool cv = s_.Identity().capabilities & 32;
        for(int o = 0; o < 2; ++o) {
            const SqCal& c = s_.Cal(o);
            const std::string values[] = {CalText(c.offset), CalText(c.scale), std::to_string(c.gate_low),
                                          std::to_string(c.gate_high), std::to_string(c.clamp_min),
                                          std::to_string(c.clamp_max)};
            for(int f = 0; f < 6; ++f) {
                if(!cal_edit_[o][f].hasKeyboardFocus(true) && (c.valid || cal_edit_[o][f].isEmpty()))
                    cal_edit_[o][f].setText(c.valid ? juce::String(values[f]) : juce::String(kCalDefaults[f]), false);
                cal_edit_[o][f].setVisible(cv);
                cal_label_[o][f].setVisible(cv);
            }
            cal_send_[o].setVisible(cv);
        }
        cv_on_.setVisible(cv);
        cv_off_.setVisible(cv);
        cal_hint_.setText(cv ? U("Run diagnostic H14 before the first connection to a synth. The enable lasts until the next restart.")
                             : U("This device has no CV outputs."),
                          juce::dontSendNotification);
        const bool ambient = s_.HasComponent("ambient-reverb");
        const Tab* tabs = ambient ? kAmbientTabs : (sw ? kStreichwurstTabs : kAcidTabs);
        if(sw != showing_streichwurst_ || ambient != showing_ambient_) {
            showing_streichwurst_ = sw;
            showing_ambient_ = ambient;
            tab_ = 0;
            rebuild_ = true;
        }
        for(int t = 0; t < 5; ++t) {
            tabs_[t].setButtonText(U(tabs[t].name));
            tabs_[t].setVisible(s_.HasComponent(tabs[t].component));
            tabs_[t].setColour(juce::TextButton::buttonColourId, t == tab_ ? colour::accent : colour::panel);
        }
        if(!s_.HasComponent(tabs[tab_].component))
            for(int t = 0; t < 5; ++t)
                if(s_.HasComponent(tabs[t].component)) {
                    tab_ = t;
                    rebuild_ = true;
                    break;
                }
        if(rebuild_ || rows_.empty())
            BuildParams();
        else
            UpdateParams();
    }

    void resized() override {
        juce::Rectangle<int> r = getLocalBounds().reduced(14);
        juce::Rectangle<int> top = r.removeFromTop(30);
        pull_.setBounds(top.removeFromRight(140));
        top.removeFromRight(8);
        stop_.setBounds(top.removeFromRight(80));
        top.removeFromRight(8);
        start_.setBounds(top.removeFromRight(80));
        status_.setBounds(top);
        r.removeFromTop(10);
        juce::Rectangle<int> grid = r.removeFromTop(110);
        const int w = grid.getWidth() / 8;
        for(int i = 0; i < 16; ++i)
            steps_[std::size_t(i)]->setBounds(grid.getX() + (i % 8) * w, grid.getY() + (i / 8) * 55, w - 6, 50);
        r.removeFromTop(6);
        juce::Rectangle<int> ed = r.removeFromTop(28);
        step_label_.setBounds(ed.removeFromLeft(80));
        note_.setBounds(ed.removeFromLeft(90));
        ed.removeFromLeft(8);
        gate_len_.setBounds(ed.removeFromLeft(260));
        for(juce::ToggleButton* b : {&gate_, &accent_, &slide_}) {
            ed.removeFromLeft(8);
            b->setBounds(ed.removeFromLeft(90));
        }
        r.removeFromTop(10);
        juce::Rectangle<int> bottom = r.removeFromBottom(250);
        juce::Rectangle<int> tabs = r.removeFromTop(28);
        for(auto& t : tabs_) {
            t.setBounds(tabs.removeFromLeft(110));
            tabs.removeFromLeft(6);
        }
        r.removeFromTop(6);
        viewport_.setBounds(r);
        param_box_.setSize(r.getWidth() - 16, std::max(r.getHeight(), int(rows_.size() + 1) / 2 * 34 + 10));
        LayoutParams();
        // bottom: calibration (left), patch and diagnostics (right)
        juce::Rectangle<int> cal = bottom.removeFromLeft(bottom.getWidth() / 2);
        for(int o = 0; o < 2; ++o) {
            juce::Rectangle<int> col = cal.removeFromLeft(cal.getWidth() / (2 - o)).reduced(4, 0);
            for(int f = 0; f < 6; ++f) {
                juce::Rectangle<int> row = col.removeFromTop(26);
                cal_label_[o][f].setBounds(row.removeFromLeft(110));
                cal_edit_[o][f].setBounds(row.removeFromLeft(80));
            }
            cal_send_[o].setBounds(col.removeFromTop(28).removeFromLeft(190));
        }
        juce::Rectangle<int> right = bottom;
        juce::Rectangle<int> b1 = right.removeFromTop(28);
        cv_on_.setBounds(b1.removeFromLeft(120));
        b1.removeFromLeft(6);
        cv_off_.setBounds(b1.removeFromLeft(120));
        cal_hint_.setBounds(right.removeFromTop(40));
        juce::Rectangle<int> b2 = right.removeFromTop(28);
        save_.setBounds(b2.removeFromLeft(140));
        b2.removeFromLeft(6);
        load_.setBounds(b2.removeFromLeft(140));
        b2.removeFromLeft(6);
        upload_.setBounds(b2.removeFromLeft(170));
        patch_info_.setBounds(right.removeFromTop(40));
        stats_btn_.setBounds(right.removeFromTop(28).removeFromLeft(140));
        stats_.setBounds(right);
    }
    void paint(juce::Graphics& g) override { g.fillAll(colour::bg); }

    // Loaded patch (file or VST3 song state), waiting for an explicit upload.
    void SetPendingPatch(const AcidPatch& p) {
        pending_ = p;
        const AcidCompat c = CheckAcidPatch(p, s_);
        juce::String text = U("Loaded: ") + U(p.product) + ", " + juce::String(int(p.components.size())) + U(" components. ");
        if(c.Clean())
            text << U("Fits completely.");
        else
            text << U("Skipped: ") << int(c.missing.size() + c.incompatible.size() + c.unknown.size()) << U(" entries.");
        patch_info_.setText(text, juce::dontSendNotification);
        upload_.setEnabled(true);
    }
    // Explicit action: sends the pending patch. Returns the number of values sent.
    int Upload() {
        if(!pending_)
            return 0;
        const int n = UploadAcidPatch(*pending_, s_);
        juce::String text = juce::String(n) + U(" values sent.");
        if(!pending_->matrix.empty()) {
            if(upload_matrix) {
                upload_matrix(pending_->matrix);
                text << U(" Matrix sent.");
            } else {
                text << U(" Matrix skipped (no Mod on Daisy connection).");
            }
        }
        patch_info_.setText(text, juce::dontSendNotification);
        return n;
    }
    int ParamRows() const { return int(rows_.size()); }
    juce::Slider* ParamSlider(const std::string& key) {
        for(auto& r : rows_)
            if(r.key == key && r.slider)
                return r.slider.get();
        return nullptr;
    }
    void SelectTab(int t) {
        tab_ = t;
        rebuild_ = true;
        Refresh();
    }

    // The product or the standalone Streichwurst FX firmware (FX page only).
    bool Streichwurst() const { return s_.HasComponent("streichwurst-instrument") || s_.HasComponent("streichwurst-fx"); }
    int TabIndex() const { return tab_; }

  private:
    // A page: the component and, for Streichwurst, the contract page of its parameters.
    struct Tab {
        const char* name;
        const char* component;
        const char* page; // "" = every parameter of the component
    };
    static constexpr Tab kAcidTabs[5] = {
        {"Sequencer", "step-seq", ""}, {"Voice", "acid-303", ""}, {"Drive", "drive", ""}, {"Delay", "stereo-delay", ""},
        {"MIDI", "step-seq", ""}};
    static constexpr Tab kStreichwurstTabs[5] = {{"Strings", "streichwurst-instrument", "strings"},
                                                 {"Solo", "streichwurst-instrument", "solo"},
                                                 {"Keyboard", "streichwurst-instrument", "keyboard"},
                                                 {"FX", "streichwurst-fx", ""},
                                                 {"Global", "streichwurst", ""}};
    static constexpr Tab kAmbientTabs[5] = {{"Ambient FX", "ambient-reverb", ""},
        {"", "", ""}, {"", "", ""}, {"", "", ""}, {"", "", ""}};
    const Tab& Current() const {
        return (showing_ambient_ ? kAmbientTabs : (showing_streichwurst_ ? kStreichwurstTabs : kAcidTabs))[tab_];
    }
    static constexpr const char* kCalDefaults[6] = {"2048", "-409.6", "2048", "410", "0", "4095"};
    static bool MidiKey(const std::string& k) {
        return k == "seq_channel" || k == "voice_channel" || k == "usb_out" || k == "din_out" || k == "velocity" ||
               k == "accent_velocity" || k == "clock_out";
    }
    struct Row {
        std::string key;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
    };
    void BuildParams() {
        rebuild_ = false;
        rows_.clear();
        const Tab& tab = Current();
        const int ci = s_.ComponentIndex(tab.component);
        if(ci < 0)
            return;
        const std::size_t comp = std::size_t(ci);
        for(const SqParam& p : s_.Components()[comp].parameters) {
            if(!showing_streichwurst_ && tab.component == std::string("step-seq") && MidiKey(p.key) != (tab_ == 4))
                continue;
            const SqLabel* german = FindSqLabel(tab.component, p.key.c_str());
            if(tab.page[0] && (!german || std::string(german->page) != tab.page))
                continue;
            Row row;
            row.key = p.key;
            row.label = std::make_unique<juce::Label>(juce::String(), german ? U(german->label) : U(p.key));
            row.label->setTooltip(U(std::string(tab.component) + "." + p.key + (p.cc != 127 ? " · CC " + std::to_string(p.cc) : "") +
                                    (p.Readonly() ? " · read only (state on the kit)" : "")));
            param_box_.addAndMakeVisible(*row.label);
            const std::string key = p.key;
            if(p.type == 2) {
                row.combo = std::make_unique<juce::ComboBox>();
                for(std::size_t v = 0; v < p.enum_keys.size(); ++v)
                    row.combo->addItem(german && int(v) < german->enum_count ? U(german->enum_labels[v]) : U(p.enum_keys[v]),
                                       int(v) + 1);
                row.combo->setEnabled(!p.Readonly());
                row.combo->onChange = [this, comp, key, c = row.combo.get()] { s_.SetParam(comp, key, c->getSelectedId() - 1); };
                param_box_.addAndMakeVisible(*row.combo);
            } else if(p.type == 3) {
                row.toggle = std::make_unique<juce::ToggleButton>(U("on"));
                row.toggle->setEnabled(!p.Readonly());
                row.toggle->onClick = [this, comp, key, t = row.toggle.get()] { s_.SetParam(comp, key, t->getToggleState() ? 1 : 0); };
                param_box_.addAndMakeVisible(*row.toggle);
            } else {
                row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                row.slider->setRange(p.min / 1000.0, p.max / 1000.0, p.type == 1 ? 1.0 : 0.0);
                if(p.Log())
                    row.slider->setSkewFactorFromMidPoint(std::sqrt(double(p.min) * double(p.max)) / 1000.0);
                row.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
                row.slider->setEnabled(!p.Readonly());
                if(p.type != 1) // wide ranges (Hz, ms) show one decimal, narrow ones two
                    row.slider->setNumDecimalPlacesToDisplay(p.max - p.min >= 100000 ? 1 : 2);
                row.slider->onDragEnd = [this, comp, key, sl = row.slider.get()] { s_.SetParam(comp, key, sl->getValue()); };
                row.slider->onValueChange = [this, comp, key, sl = row.slider.get()] {
                    if(!sl->isMouseButtonDown())
                        s_.SetParam(comp, key, sl->getValue()); // typed into the text box
                };
                param_box_.addAndMakeVisible(*row.slider);
            }
            rows_.push_back(std::move(row));
        }
        resized();
        UpdateParams();
    }
    void UpdateParams() {
        const int ci = s_.ComponentIndex(Current().component);
        if(ci < 0)
            return;
        for(auto& row : rows_) {
            const SqParam* p = s_.Components()[std::size_t(ci)].Find(row.key);
            if(!p)
                continue;
            if(row.slider && !row.slider->isMouseButtonDown())
                row.slider->setValue(p->Value(), juce::dontSendNotification);
            if(row.combo)
                row.combo->setSelectedId(int(p->Value() + 0.5) + 1, juce::dontSendNotification);
            if(row.toggle)
                row.toggle->setToggleState(p->Value() >= 0.5, juce::dontSendNotification);
        }
    }
    void LayoutParams() {
        const int col_w = param_box_.getWidth() / 2;
        for(std::size_t i = 0; i < rows_.size(); ++i) {
            juce::Rectangle<int> r(int(i % 2) * col_w, int(i / 2) * 34 + 4, col_w - 12, 28);
            rows_[i].label->setBounds(r.removeFromLeft(130));
            juce::Component* c = rows_[i].slider ? static_cast<juce::Component*>(rows_[i].slider.get())
                                 : rows_[i].combo ? static_cast<juce::Component*>(rows_[i].combo.get())
                                                  : static_cast<juce::Component*>(rows_[i].toggle.get());
            c->setBounds(r);
        }
    }
    void EditStep(const std::function<void(step_seq::Step&)>& change) {
        step_seq::Step st = s_.Pattern()[std::size_t(selected_)];
        change(st);
        s_.SetStep(selected_, st);
    }
    void SendCal(int o) {
        SqCal c;
        c.offset = CalMilli(cal_edit_[o][0].getText().getDoubleValue());
        c.scale = CalMilli(cal_edit_[o][1].getText().getDoubleValue());
        c.gate_low = uint16_t(cal_edit_[o][2].getText().getIntValue());
        c.gate_high = uint16_t(cal_edit_[o][3].getText().getIntValue());
        c.clamp_min = uint16_t(cal_edit_[o][4].getText().getIntValue());
        c.clamp_max = uint16_t(cal_edit_[o][5].getText().getIntValue());
        s_.SetCal(o, c);
    }
    void RefreshStats() {
        static const char* names[] = {"Frame length", "Checksum", "Version", "Unknown type", "Too long", "Status byte",
                                      "Repeat", "Echo", "Busy", "Monitor dropped", "Note-on dropped",
                                      "MIDI queue overflow", "Clock ignored", "Callback deadline", "Callback peak",
                                      "Queue high water", "Stack free"};
        juce::String text;
        for(std::size_t i = 0; i < s_.Stats().size(); ++i)
            text << U(i < 17 ? names[i] : "Counter") << ": " << juce::String(juce::int64(s_.Stats()[i])) << (i % 2 ? "\n" : "    ");
        stats_.setText(text, juce::dontSendNotification);
    }
  public:
    // The device state as product patch; Streichwurst adds the matrix of the MD session.
    AcidPatch CurrentPatch() const {
        AcidPatch p = PatchFromSession(s_);
        if(Streichwurst() && matrix_text)
            p.matrix = matrix_text();
        return p;
    }

  private:
    void SaveDialog() {
        const std::string ext = PatchExtension(s_.Identity().product);
        chooser_ = std::make_unique<juce::FileChooser>(U(Streichwurst() ? "Save Streichwurst patch" : "Save Acid Seq patch"),
                                                       juce::File(), U("*." + ext));
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, ext](const juce::FileChooser& c) {
                                  const juce::File f = c.getResult();
                                  if(f != juce::File())
                                      f.withFileExtension(U(ext)).replaceWithText(U(SerializeAcidPatch(CurrentPatch())));
                              });
    }
    void LoadDialog() {
        chooser_ = std::make_unique<juce::FileChooser>(U(Streichwurst() ? "Load Streichwurst patch" : "Load Acid Seq patch"),
                                                       juce::File(), "*.acidpatch;*.swpatch;*.json");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode, [this](const juce::FileChooser& c) {
            const juce::File f = c.getResult();
            if(f == juce::File())
                return;
            std::string error;
            if(auto p = ParseAcidPatch(f.loadFileAsString().toStdString(), error))
                SetPendingPatch(*p);
            else
                patch_info_.setText(U("Not readable: ") + U(error), juce::dontSendNotification);
        });
    }

    SqSession& s_;
    juce::Label status_, patch_info_, stats_, cal_hint_, step_label_;
    juce::TextButton start_{"Start"}, stop_{U("Stop")}, pull_{U("Read from device")}, save_{U("Save patch …")},
        load_{U("Load patch …")}, upload_{U("Send to device")}, stats_btn_{U("Read counters")},
        cv_on_{U("Enable CV")}, cv_off_{U("Lock CV")};
    juce::TextButton cal_send_[2] = {juce::TextButton(U("Send CV OUT 1")), juce::TextButton(U("Send CV OUT 2"))};
    juce::Label cal_label_[2][6];
    juce::TextEditor cal_edit_[2][6];
    std::vector<std::unique_ptr<juce::TextButton>> steps_;
    juce::ComboBox note_;
    juce::Slider gate_len_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::ToggleButton gate_{"Gate"}, accent_{"Accent"}, slide_{"Slide"};
    juce::TextButton tabs_[5];
    juce::Viewport viewport_;
    juce::Component param_box_;
    std::vector<Row> rows_;
    std::optional<AcidPatch> pending_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int selected_ = 0, tab_ = 0;
    bool rebuild_ = true, showing_streichwurst_ = false, showing_ambient_ = false;
};

} // namespace editor::ui
