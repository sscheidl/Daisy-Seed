#pragma once
// The SD card page of the Daisy Control Panel: shares the instrument's microSD with Windows
// and brings it back. Texts, rows and button rules are the ones of Daisy Diagnostic 0.6.0's
// "SD share" tab (docs/dcp/SD_SHARE_PORT.md § 6). The page only shows the workflow's
// state; every action goes through the callbacks, so the editor component can save the
// patch first (D5) and ask the confirmation questions. In the VST3 the page is not
// available and shows the note of decision D2 instead.
#include "../core/DdpSession.h"
#include "../core/SdShare.h"
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdio>
#include <functional>

namespace editor::ui {
class SdSharePanel : public juce::Component {
  public:
    static constexpr const char* kUnavailableNote =
        "Sharing the SD card needs the Daisy Control Panel app (the kit restarts, and the DAW loses the Daisy MIDI port)";

    // Actions, wired by the editor component (nullptr = disabled).
    std::function<void(bool confirm)> request_share;
    std::function<void()> cancel, return_to_performance, acknowledge, eject_help;

    SdSharePanel(sd::SdShareWorkflow& workflow, ddp::DdpSession& session, bool available, const juce::String& about)
        : wf_(workflow), session_(session), available_(available) {
        setOpaque(true);
        auto label = [this](juce::Label& l, juce::Colour c, float size, bool bold = false, bool wrap = true) {
            addAndMakeVisible(l);
            l.setColour(juce::Label::textColourId, c);
            l.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
            l.setJustificationType(wrap ? juce::Justification::topLeft : juce::Justification::centredLeft);
            l.setMinimumHorizontalScale(1.0f);
        };
        label(heading_, colour::ink, 17.0f, true, false);
        for(std::size_t i = 0; i < kRows; ++i) {
            label(row_labels_[i], colour::muted, 13.0f, false, false);
            label(rows_[i], colour::ink, 13.0f);
            row_labels_[i].setText(juce::String(kRowNames[i]) + ":", juce::dontSendNotification);
        }
        label(message_, colour::ink, 13.0f);
        label(error_, colour::err, 13.0f, true);
        label(warning_, colour::err, 13.0f, true);
        label(hint_, colour::muted, 12.0f);
        label(about_, colour::muted, 12.0f, false, false);
        label(note_, colour::warn, 14.0f, true);
        hint_.setText(U("Performance mode uses USB-MIDI (no COM port); SD management uses USB CDC + mass storage. The card is "
                        "never returned by closing this window or unplugging: only a Windows eject or an explicit "
                        "acknowledgement. Power-cycling the kit always starts in performance."),
                      juce::dontSendNotification);
        about_.setText(about, juce::dontSendNotification);
        note_.setText(U(kUnavailableNote), juce::dontSendNotification);

        auto button = [this](juce::TextButton& b, const char* text, std::function<void()> click) {
            addAndMakeVisible(b);
            b.setButtonText(U(text));
            b.onClick = std::move(click);
        };
        button(share_, "Share SD card with Windows", [this] {
            if(request_share)
                request_share(false);
        });
        share_.setColour(juce::TextButton::buttonColourId, colour::accent);
        share_.setTooltip(U("Stops audio, restarts the instrument into SD management and opens the card as a Windows drive (USB "
                            "mass storage). The looper saves its loop to the card first."));
        button(confirm_, "Discard Live loop and share", [this] {
            if(request_share)
                request_share(true);
        });
        button(cancel_, "Cancel", [this] {
            if(cancel)
                cancel();
        });
        button(eject_help_, "How to eject safely in Windows …", [this] {
            if(eject_help)
                eject_help();
        });
        button(return_, "Return to Nebulae", [this] {
            if(return_to_performance)
                return_to_performance();
        });
        return_.setTooltip(U("Only after Windows ejected the card: the firmware returns it and restarts into performance "
                             "(Nebulae rescans its banks, the looper reloads looper/loop.wav)."));
        button(ack_, "Acknowledge interrupted access …", [this] {
            if(acknowledge)
                acknowledge();
        });
        Refresh();
    }

    void SetSimulated(bool simulated) { simulated_ = simulated; }
    void SetAbout(const juce::String& text) { about_.setText(text, juce::dontSendNotification); }

    // Applies the rules of SD_SHARE_PORT.md § 6.2 to the controls.
    void Refresh() {
        if(!available_) {
            for(juce::Component* c : Controls())
                c->setVisible(false);
            note_.setVisible(true);
            about_.setVisible(true);
            return;
        }
        note_.setVisible(false);
        using sd::Phase;
        const Phase phase = wf_.GetPhase();
        const bool perf = phase == Phase::Performance || phase == Phase::Confirm || phase == Phase::Requesting || phase == Phase::Saving;
        const std::optional<sd::PerfStatus>& st_opt = wf_.Status();
        const sd::PerfStatus* st = perf && st_opt ? &*st_opt : nullptr;
        const bool mgmt = wf_.ManagementConnected();
        const ddp::DeviceInfo* dev = mgmt && session_.Device() ? &*session_.Device() : nullptr;
        const ddp::MgmtInfo* neb = mgmt && session_.Mgmt() ? &*session_.Mgmt() : nullptr;
        const bool looper = wf_.GetProfile().id == sd::Instrument::Looper;
        const bool found = st || mgmt;
        heading_.setText(U(sd::PhaseText(phase)) + (simulated_ ? U(" – SIMULATION") : U("")), juce::dontSendNotification);
        Row(0, found ? U(wf_.GetProfile().display_name) : U("–"));
        row_labels_[4].setText(looper ? U("Loop:") : U("Bank / sample:"), juce::dontSendNotification);
        return_.setButtonText(looper ? U("Return to the Looper") : U("Return to Nebulae"));
        confirm_.setButtonText(looper ? U("Share without the unsaved changes") : U("Discard Live loop and share"));
        Row(1, st ? U(st->firmware) : dev ? U(dev->firmware) : U("–"));
        Row(2, st ? U("USB-MIDI ") + U(wf_.Port()) : mgmt ? U("USB CDC ") + U(session_.Port()) + U(" + mass storage") : U("–"));
        const std::optional<ddp::ReaderState> reader = wf_.ReaderState();
        const juce::String counters =
            st ? U("peak ") + juce::String(st->peak_load) + U(" % of the block, ") + juce::String(st->overruns)
                     + U(" overruns (firmware counters, not a bench measurement)") + (st->overrun ? U(" – OVERRUN LED latched") : U(""))
               : juce::String();
        if(st && looper) {
            Row(3, st->card ? U("card inserted") : U("no card"));
            juce::String loop;
            if(!st->loop && !st->unsaved) {
                loop = U("no loop");
            } else {
                const juce::String state = st->saving ? U("saving to looper/loop.wav …")
                                           : st->unsaved ? U("not saved yet")
                                                         : U("saved in looper/loop.wav");
                char seconds[32];
                std::snprintf(seconds, sizeof(seconds), "%.1f s, ", double(st->loop_ms) / 1000.0);
                loop = st->loop ? U(seconds) + state : U("erased, ") + state;
            }
            if(st->Busy())
                loop << (st->recording ? U("; recording") : U("; importing a WAV file"));
            if(st->save_failed)
                loop << U("; last card write failed (") << U(st->FaultText()) << U(")");
            Row(4, loop);
            Row(5, counters);
        } else if(st) {
            Row(3, st->card ? U("card inserted") : U("no card"));
            juce::String bank = U("bank ") + juce::String(st->bank) + U(" of ") + juce::String(st->banks);
            if(st->playing_slot)
                bank << U(", playing bank ") << juce::String(st->playing_bank) << U(" slot ") << juce::String(st->playing_slot);
            if(st->load_error)
                bank << U("; last load: ") << U(st->LoadErrorText());
            Row(4, bank);
            Row(5, counters);
        } else if(mgmt) {
            Row(3, U("card reader: ") + (reader ? U(ddp::ReaderStateName(*reader)) : U("unknown")));
            if(looper)
                Row(4, U("reloaded from looper/loop.wav after the return"));
            else
                Row(4, neb && neb->bank ? U("restores bank ") + juce::String(neb->bank) + U(" slot ") + juce::String(neb->slot) : U("–"));
            Row(5, U("audio stopped (SD management)"));
        } else {
            for(std::size_t i = 3; i < kRows; ++i)
                Row(i, U("–"));
        }
        message_.setText(U(wf_.Message()), juce::dontSendNotification);
        error_.setText(U(wf_.Error()), juce::dontSendNotification);
        if(looper)
            warning_.setText(wf_.NeedsConfirmation() || (st && st->save_failed && st->unsaved)
                                 ? U("The loop could not be saved to the card. Sharing restarts the looper and LOSES the unsaved "
                                     "changes; looper/loop.wav keeps the last saved loop.")
                                 : U(""),
                             juce::dontSendNotification);
        else
            warning_.setText((st && st->live) || wf_.NeedsConfirmation()
                                 ? U("A Live loop exists only in RAM. Sharing restarts Nebulae and LOSES it; the SD samples are "
                                     "reloaded from the card.")
                                 : U(""),
                             juce::dontSendNotification);
        share_.setEnabled((wf_.CanShare() && !wf_.NeedsConfirmation()) || wf_.CanShareInManagement());
        share_.setColour(juce::TextButton::buttonColourId, share_.isEnabled() ? colour::accent : colour::raised);
        confirm_.setVisible(wf_.NeedsConfirmation());
        cancel_.setVisible(wf_.NeedsConfirmation() || phase == Phase::Saving);
        eject_help_.setEnabled(reader == ddp::ReaderState::Shared);
        return_.setEnabled(wf_.CanReturn());
        ack_.setEnabled(wf_.CanAcknowledge());
        resized();
        repaint();
    }

    // ------------------------------------------------------------------ for --selftest
    juce::TextButton& ShareButton() { return share_; }
    juce::TextButton& ConfirmButton() { return confirm_; }
    juce::TextButton& CancelButton() { return cancel_; }
    juce::TextButton& EjectHelpButton() { return eject_help_; }
    juce::TextButton& ReturnButton() { return return_; }
    juce::TextButton& AckButton() { return ack_; }
    juce::String Heading() const { return heading_.getText(); }
    juce::String RowText(int i) const { return rows_[std::size_t(i)].getText(); }
    juce::String RowLabel(int i) const { return row_labels_[std::size_t(i)].getText(); }
    juce::String MessageText() const { return message_.getText(); }
    juce::String ErrorText() const { return error_.getText(); }
    juce::String WarningText() const { return warning_.getText(); }
    juce::String NoteText() const { return note_.isVisible() ? note_.getText() : juce::String(); }
    // Every visible control lies inside the page (test_long_texts_stay_inside_the_panel).
    bool AllVisibleInside() const {
        for(int i = 0; i < getNumChildComponents(); ++i) {
            const juce::Component* c = getChildComponent(i);
            if(c->isVisible() && !getLocalBounds().contains(c->getBounds()))
                return false;
        }
        return true;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::bg);
        PaintCard(g, card_, available_ ? "SD card" : "SD card (standalone app only)");
    }

    void resized() override {
        juce::Rectangle<int> r = getLocalBounds().reduced(14);
        card_ = r.withWidth(juce::jmin(r.getWidth(), 760)).withX(r.getCentreX() - juce::jmin(r.getWidth(), 760) / 2);
        juce::Rectangle<int> c = card_.reduced(18, 12).withTrimmedTop(26);
        about_.setBounds(c.removeFromBottom(18));
        if(!available_) {
            note_.setBounds(c.removeFromTop(60));
            return;
        }
        heading_.setBounds(c.removeFromTop(28));
        c.removeFromTop(6);
        for(std::size_t i = 0; i < kRows; ++i) {
            juce::Rectangle<int> row = c.removeFromTop(i == 4 || i == 5 ? 36 : 22);
            row_labels_[i].setBounds(row.removeFromLeft(130));
            rows_[i].setBounds(row);
        }
        c.removeFromTop(8);
        message_.setBounds(c.removeFromTop(Lines(message_, c.getWidth(), 3)));
        c.removeFromTop(4);
        error_.setBounds(c.removeFromTop(Lines(error_, c.getWidth(), 3)));
        c.removeFromTop(4);
        warning_.setBounds(c.removeFromTop(Lines(warning_, c.getWidth(), 2)));
        c.removeFromTop(8);
        share_.setBounds(c.removeFromTop(32));
        c.removeFromTop(6);
        juce::Rectangle<int> row = c.removeFromTop(32);
        cancel_.setBounds(row.removeFromRight(110));
        row.removeFromRight(8);
        confirm_.setBounds(row);
        c.removeFromTop(6);
        eject_help_.setBounds(c.removeFromTop(32));
        c.removeFromTop(6);
        return_.setBounds(c.removeFromTop(32));
        c.removeFromTop(6);
        ack_.setBounds(c.removeFromTop(32));
        c.removeFromTop(10);
        hint_.setBounds(c.removeFromTop(juce::jmin(c.getHeight(), 64)));
    }

  private:
    static constexpr std::size_t kRows = 6;
    static constexpr const char* kRowNames[kRows] = {"Instrument", "Firmware", "Connection", "SD card", "Bank / sample",
                                                     "Audio deadline"};
    void Row(std::size_t i, const juce::String& text) { rows_[i].setText(text, juce::dontSendNotification); }
    // Height for up to `max_lines` wrapped lines of the label's text at `width`, never 0.
    static int Lines(const juce::Label& l, int width, int max_lines) {
        if(l.getText().isEmpty() || width <= 0)
            return 0;
        const juce::Font font(l.getFont());
        const float text_width = juce::GlyphArrangement::getStringWidth(font, l.getText());
        const int lines = juce::jlimit(1, max_lines, int(text_width / float(width)) + 1);
        return int(font.getHeight() * 1.25f) * lines + 4;
    }
    std::vector<juce::Component*> Controls() {
        std::vector<juce::Component*> all = {&heading_, &message_, &error_, &warning_, &hint_, &share_, &confirm_,
                                             &cancel_, &eject_help_, &return_, &ack_};
        for(std::size_t i = 0; i < kRows; ++i) {
            all.push_back(&row_labels_[i]);
            all.push_back(&rows_[i]);
        }
        return all;
    }

    sd::SdShareWorkflow& wf_;
    ddp::DdpSession& session_;
    bool available_, simulated_ = false;
    juce::Label heading_, message_, error_, warning_, hint_, about_, note_;
    std::array<juce::Label, kRows> row_labels_, rows_;
    juce::TextButton share_, confirm_, cancel_, eject_help_, return_, ack_;
    juce::Rectangle<int> card_;
};
}
