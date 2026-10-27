#pragma once
// Acid Seq pages of the JUCE editor and the VST3 (ARBEITSAUFTRAG Phase 5): status and
// transport, 16-step pattern grid with note names and play position, step editor,
// parameter pages from the device's descriptors (Sequencer, Stimme, Drive, Delay, MIDI),
// calibration with the separate CV enable, product patch save/load/upload, counters.
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
            tabs_[t].setButtonText(U(kTabNames[t]));
            tabs_[t].setClickingTogglesState(false);
            tabs_[t].onClick = [this, t] {
                tab_ = t;
                rebuild_ = true;
                Refresh();
            };
            addAndMakeVisible(tabs_[t]);
        }
        static const char* fields[] = {"Code bei 0 V", "Codes pro Volt", "Gate-Low", "Gate-High", "Klemme min", "Klemme max"};
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
        juce::String text = U(s_.Identity().product) + " " + U(s_.Identity().version);
        if(seq)
            text << U(" · ") << U(SqStateName(st.state)) << U(" · SW17 ") << (st.sw17 ? "Run" : "Stop") << U(" · ")
                 << U(st.mode == 0 ? "Vorwärts" : st.mode == 1 ? "Rückwärts" : "Zufall") << U(" · ")
                 << juce::String(st.bpm, 1) << U(" BPM · Revision ") << int(st.applied) << "/" << int(st.accepted)
                 << U(st.cv_enabled ? " · CV frei" : " · CV gesperrt");
        status_.setText(text, juce::dontSendNotification);
        start_.setEnabled(seq);
        stop_.setEnabled(seq);
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
            const int32_t values[] = {c.offset / 1000, c.scale / 1000, c.gate_low, c.gate_high, c.clamp_min, c.clamp_max};
            for(int f = 0; f < 6; ++f) {
                if(!cal_edit_[o][f].hasKeyboardFocus(true) && (c.valid || cal_edit_[o][f].isEmpty()))
                    cal_edit_[o][f].setText(juce::String(c.valid ? values[f] : kCalDefaults[f]), false);
                cal_edit_[o][f].setVisible(cv);
                cal_label_[o][f].setVisible(cv);
            }
            cal_send_[o].setVisible(cv);
        }
        cv_on_.setVisible(cv);
        cv_off_.setVisible(cv);
        cal_hint_.setText(cv ? U("Vor dem ersten Anschluss an einen Synth die Diagnose H14 durchführen. Die Freigabe gilt bis zum Neustart.")
                             : U("Dieses Gerät hat keine CV-Ausgänge."),
                          juce::dontSendNotification);
        for(int t = 0; t < 5; ++t) {
            tabs_[t].setVisible(s_.HasComponent(kTabComponents[t]));
            tabs_[t].setColour(juce::TextButton::buttonColourId, t == tab_ ? colour::accent : colour::panel);
        }
        if(!s_.HasComponent(kTabComponents[tab_]))
            for(int t = 0; t < 5; ++t)
                if(s_.HasComponent(kTabComponents[t])) {
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
        juce::String text = U("Geladen: ") + U(p.product) + ", " + juce::String(int(p.components.size())) + U(" Komponenten. ");
        if(c.Clean())
            text << U("Passt vollständig.");
        else
            text << U("Übersprungen: ") << int(c.missing.size() + c.incompatible.size() + c.unknown.size()) << U(" Einträge.");
        patch_info_.setText(text, juce::dontSendNotification);
        upload_.setEnabled(true);
    }
    // Explicit action: sends the pending patch. Returns the number of values sent.
    int Upload() {
        if(!pending_)
            return 0;
        const int n = UploadAcidPatch(*pending_, s_);
        patch_info_.setText(juce::String(n) + U(" Werte übertragen."), juce::dontSendNotification);
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

  private:
    static constexpr const char* kTabNames[5] = {"Sequencer", "Stimme", "Drive", "Delay", "MIDI"};
    static constexpr const char* kTabComponents[5] = {"step-seq", "acid-303", "drive", "stereo-delay", "step-seq"};
    static constexpr int kCalDefaults[6] = {2048, -410, 2048, 410, 0, 4095};
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
        const int ci = s_.ComponentIndex(kTabComponents[tab_]);
        if(ci < 0)
            return;
        const std::size_t comp = std::size_t(ci);
        for(const SqParam& p : s_.Components()[comp].parameters) {
            if(kTabComponents[tab_] == std::string("step-seq") && MidiKey(p.key) != (tab_ == 4))
                continue;
            Row row;
            row.key = p.key;
            const SqLabel* german = FindSqLabel(kTabComponents[tab_], p.key.c_str());
            row.label = std::make_unique<juce::Label>(juce::String(), german ? U(german->label) : U(p.key));
            row.label->setTooltip(U(std::string(kTabComponents[tab_]) + "." + p.key + (p.cc != 127 ? " · CC " + std::to_string(p.cc) : "")));
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
                row.toggle = std::make_unique<juce::ToggleButton>(U("an"));
                row.toggle->onClick = [this, comp, key, t = row.toggle.get()] { s_.SetParam(comp, key, t->getToggleState() ? 1 : 0); };
                param_box_.addAndMakeVisible(*row.toggle);
            } else {
                row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                row.slider->setRange(p.min / 1000.0, p.max / 1000.0, p.type == 1 ? 1.0 : 0.0);
                if(p.Log())
                    row.slider->setSkewFactorFromMidPoint(std::sqrt(double(p.min) * double(p.max)) / 1000.0);
                row.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
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
        const int ci = s_.ComponentIndex(kTabComponents[tab_]);
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
        c.offset = cal_edit_[o][0].getText().getIntValue() * 1000;
        c.scale = int32_t(cal_edit_[o][1].getText().getDoubleValue() * 1000.0);
        c.gate_low = uint16_t(cal_edit_[o][2].getText().getIntValue());
        c.gate_high = uint16_t(cal_edit_[o][3].getText().getIntValue());
        c.clamp_min = uint16_t(cal_edit_[o][4].getText().getIntValue());
        c.clamp_max = uint16_t(cal_edit_[o][5].getText().getIntValue());
        s_.SetCal(o, c);
    }
    void RefreshStats() {
        static const char* names[] = {"Rahmenlänge", "Prüfsumme", "Version", "unbekannter Typ", "zu lang", "Statusbyte",
                                      "Wiederholung", "Echo", "beschäftigt", "Monitor verworfen", "Note-On verworfen",
                                      "MIDI-Queue übergelaufen", "Clock ignoriert", "Callback-Frist", "Callback-Spitze",
                                      "Queue-Hochwasser", "Stack frei"};
        juce::String text;
        for(std::size_t i = 0; i < s_.Stats().size(); ++i)
            text << U(i < 17 ? names[i] : "Zähler") << ": " << juce::String(juce::int64(s_.Stats()[i])) << (i % 2 ? "\n" : "    ");
        stats_.setText(text, juce::dontSendNotification);
    }
    void SaveDialog() {
        chooser_ = std::make_unique<juce::FileChooser>(U("Acid-Seq-Patch speichern"), juce::File(), "*.acidpatch");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& c) {
                                  const juce::File f = c.getResult();
                                  if(f != juce::File())
                                      f.withFileExtension("acidpatch").replaceWithText(U(SerializeAcidPatch(PatchFromSession(s_))));
                              });
    }
    void LoadDialog() {
        chooser_ = std::make_unique<juce::FileChooser>(U("Acid-Seq-Patch laden"), juce::File(), "*.acidpatch;*.json");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode, [this](const juce::FileChooser& c) {
            const juce::File f = c.getResult();
            if(f == juce::File())
                return;
            std::string error;
            if(auto p = ParseAcidPatch(f.loadFileAsString().toStdString(), error))
                SetPendingPatch(*p);
            else
                patch_info_.setText(U("Nicht lesbar: ") + U(error), juce::dontSendNotification);
        });
    }

    SqSession& s_;
    juce::Label status_, patch_info_, stats_, cal_hint_, step_label_;
    juce::TextButton start_{"Start"}, stop_{U("Stopp")}, pull_{U("Vom Gerät lesen")}, save_{U("Patch speichern …")},
        load_{U("Patch laden …")}, upload_{U("Auf Gerät übertragen")}, stats_btn_{U("Zähler lesen")},
        cv_on_{U("CV freigeben")}, cv_off_{U("CV sperren")};
    juce::TextButton cal_send_[2] = {juce::TextButton(U("CV OUT 1 senden")), juce::TextButton(U("CV OUT 2 senden"))};
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
    bool rebuild_ = true;
};

} // namespace editor::ui
