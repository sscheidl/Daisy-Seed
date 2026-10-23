#pragma once
// LFO 1/2 and Random (GUI.md § 2.3): only the controls that fit the chosen sync
// source are visible: rate and retrigger when free, division with MIDI clock,
// gate input and cycles per pulse with a gate.
#include "../core/Session.h"
#include "Theme.h"

namespace editor::ui {
class SourcePanel : public juce::Component {
  public:
    SourcePanel(Model& model, Session& session, int src) : model_(model), session_(session), src_(src) {
        const bool random = src == mod::Random;
        const int shapes = random ? int(mod::kRandomModeCount) : int(mod::kShapeCount);
        for(int i = 0; i < shapes; ++i)
            shape_.addItem(U(random ? kRandomModeNames[i] : kShapeNames[i]), i + 1);
        shape_.onChange = [this] { Edit(mod::ParamShape, unsigned(shape_.getSelectedId() - 1)); };
        for(int i = 0; i < mod::kSyncCount; ++i) {
            auto& b = sync_[std::size_t(i)];
            b.setButtonText(U(kSyncNames[i]));
            b.setClickingTogglesState(false);
            b.setConnectedEdges((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                | (i < mod::kSyncCount - 1 ? juce::Button::ConnectedOnRight : 0));
            b.onClick = [this, i] { Edit(mod::ParamSync, unsigned(i)); };
            addAndMakeVisible(b);
        }
        rate_.setSliderStyle(juce::Slider::LinearHorizontal);
        rate_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        rate_.setRange(0, mod::kRateMax, 1);
        rate_.onValueChange = [this] { Edit(mod::ParamRate, unsigned(rate_.getValue())); };
        division_.onChange = [this] { Edit(mod::ParamDivision, unsigned(division_.getSelectedId() - 1)); };
        gate_.addItem("Gate 1", 1);
        gate_.addItem("Gate 2", 2);
        gate_.onChange = [this] { Edit(mod::ParamGate, unsigned(gate_.getSelectedId() - 1)); };
        retrigger_.setButtonText("bei Note On");
        retrigger_.onClick = [this] { Edit(mod::ParamRetrigger, retrigger_.getToggleState() ? 1u : 0u); };
        hz_.setJustificationType(juce::Justification::centredRight);
        for(juce::Component* c : std::initializer_list<juce::Component*>{&shape_, &rate_, &hz_, &division_, &gate_})
            addAndMakeVisible(c);
        if(!random)
            addAndMakeVisible(retrigger_);
        for(auto* l : {&shape_label_, &sync_label_, &rate_label_, &division_label_, &gate_label_, &retrigger_label_}) {
            l->setColour(juce::Label::textColourId, colour::muted);
            addAndMakeVisible(l);
        }
        shape_label_.setText(random ? "Modus" : "Form", juce::dontSendNotification);
        sync_label_.setText("Sync", juce::dontSendNotification);
        rate_label_.setText("Rate", juce::dontSendNotification);
        division_label_.setText("Teilung", juce::dontSendNotification);
        gate_label_.setText("Eingang", juce::dontSendNotification);
        retrigger_label_.setText("Retrigger", juce::dontSendNotification);
        retrigger_label_.setVisible(!random);
        Refresh();
    }

    // Shows the model without sending anything.
    void Refresh() {
        const unsigned sync = model_.SourceParam(src_, mod::ParamSync);
        updating_ = true;
        shape_.setSelectedId(int(model_.SourceParam(src_, mod::ParamShape)) + 1, juce::dontSendNotification);
        for(int i = 0; i < mod::kSyncCount; ++i)
            sync_[std::size_t(i)].setToggleState(unsigned(i) == sync, juce::dontSendNotification);
        rate_.setValue(model_.SourceParam(src_, mod::ParamRate), juce::dontSendNotification);
        const float hz = RateToHz(model_.SourceParam(src_, mod::ParamRate));
        hz_.setText(juce::String(hz, hz < 1.0f ? 3 : 2) + " Hz", juce::dontSendNotification);
        if(int(sync) != division_sync_) {
            division_.clear(juce::dontSendNotification);
            for(unsigned i = 0; i < DivisionCount(sync); ++i)
                division_.addItem(U(DivisionName(sync, i)) + (sync == mod::GateClock ? U(" Zyklen/Impuls") : U("")),
                                  int(i) + 1);
            division_sync_ = int(sync);
        }
        division_.setSelectedId(int(model_.SourceParam(src_, mod::ParamDivision)) + 1, juce::dontSendNotification);
        gate_.setSelectedId(int(model_.SourceParam(src_, mod::ParamGate)) + 1, juce::dontSendNotification);
        retrigger_.setToggleState(model_.SourceParam(src_, mod::ParamRetrigger) != 0, juce::dontSendNotification);
        updating_ = false;
        const bool free = sync == mod::Free, gate = sync == mod::GateClock;
        for(juce::Component* c : std::initializer_list<juce::Component*>{&rate_, &hz_, &rate_label_})
            c->setVisible(free);
        division_.setVisible(!free);
        division_label_.setVisible(!free);
        gate_.setVisible(gate);
        gate_label_.setVisible(gate);
        const bool retrig = free && src_ != mod::Random;
        retrigger_.setVisible(retrig);
        retrigger_label_.setVisible(retrig);
        resized();
    }

    void paint(juce::Graphics& g) override { PaintCard(g, getLocalBounds(), U(kSourceNames[src_])); }

    void resized() override {
        juce::Rectangle<int> r = getLocalBounds().reduced(14, 10).withTrimmedTop(24);
        auto row = [&](juce::Label& label) {
            juce::Rectangle<int> line = r.removeFromTop(28);
            r.removeFromTop(6);
            label.setBounds(line.removeFromLeft(72));
            return line;
        };
        shape_.setBounds(row(shape_label_));
        juce::Rectangle<int> s = row(sync_label_);
        const int w = s.getWidth() / mod::kSyncCount;
        for(int i = 0; i < mod::kSyncCount; ++i)
            sync_[std::size_t(i)].setBounds(i == mod::kSyncCount - 1 ? s : s.removeFromLeft(w));
        if(rate_.isVisible()) {
            juce::Rectangle<int> line = row(rate_label_);
            hz_.setBounds(line.removeFromRight(72));
            rate_.setBounds(line);
        }
        if(division_.isVisible())
            division_.setBounds(row(division_label_));
        if(gate_.isVisible())
            gate_.setBounds(row(gate_label_));
        if(retrigger_.isVisible())
            retrigger_.setBounds(row(retrigger_label_));
    }

    juce::ComboBox& Shape() { return shape_; }
    juce::TextButton& Sync(int i) { return sync_[std::size_t(i)]; }
    juce::ComboBox& Division() { return division_; }
    juce::ComboBox& Gate() { return gate_; }
    juce::Slider& Rate() { return rate_; }

  private:
    void Edit(int param, unsigned value) {
        if(!updating_)
            session_.EditSource(src_, param, value);
    }

    Model& model_;
    Session& session_;
    const int src_;
    bool updating_ = false;
    int division_sync_ = -1;
    juce::ComboBox shape_, division_, gate_;
    std::array<juce::TextButton, mod::kSyncCount> sync_;
    juce::Slider rate_;
    juce::Label hz_, shape_label_, sync_label_, rate_label_, division_label_, gate_label_, retrigger_label_;
    juce::ToggleButton retrigger_;
};
}
