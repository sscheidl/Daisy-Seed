#pragma once
// The 8 × 8 grid (GUI.md § 2.1/2.2): every crossing always visible, unsupported
// cells hatched, depth by vertical drag, wheel or keyboard, double-click on/off.
#include "../core/Session.h"
#include "Theme.h"
#include <functional>

namespace editor::ui {
class MatrixGrid : public juce::Component, public juce::TooltipClient {
  public:
    static constexpr int kRowHeader = 124, kColHeader = 58, kGap = 4;
    // Firmware without the extended monitor: above a quarter of the monitor rate a sampled
    // free LFO looks frozen (20 Hz sampled at 20 Hz shows one value), so the meter shows a
    // band instead. With the extended monitor the device reports the swept range itself.
    static constexpr float kFastHz = float(Session::kMonitorRate) / 4.0f;

    std::function<void(int cell)> selected; // selection changed

    MatrixGrid(Model& model, Session& session) : model_(model), session_(session) {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
    }

    void SetMonitor(const MonitorValues& m) {
        monitor_ = m;
        has_monitor_ = true;
        repaint();
    }
    void ClearMonitor() {
        has_monitor_ = false;
        repaint();
    }
    // Called about once a second: shows when the live data stops arriving.
    void CheckStale() {
        if(Stale() != painted_stale_)
            repaint();
    }
    // The session decides (1.5 s without monitor frames, 5 s without anything).
    bool Stale() const { return session_.Connected() && (!has_monitor_ || session_.LinkHealth() != Health::Ok); }
    const MonitorValues& Monitor() const { return monitor_; }
    bool HasMonitor() const { return has_monitor_; }

    juce::Rectangle<int> CellBounds(int cell) const {
        const float w = float(getWidth() - kRowHeader - 7 * kGap) / 8.0f;
        const float h = float(getHeight() - kColHeader - 7 * kGap) / 8.0f;
        const int s = SourceOf(cell), t = TargetOf(cell);
        return juce::Rectangle<float>(float(kRowHeader) + float(t) * (w + kGap), float(kColHeader) + float(s) * (h + kGap),
                                      w, h)
            .toNearestInt();
    }
    int CellAt(juce::Point<int> p) const {
        for(int c = 0; c < kCells; ++c)
            if(CellBounds(c).expanded(kGap / 2).contains(p))
                return c;
        return kNoCell;
    }

    void Select(int cell) {
        if(cell < 0 || cell >= kCells)
            return;
        model_.selected = cell;
        repaint();
        if(selected)
            selected(cell);
    }

    void paint(juce::Graphics& g) override {
        painted_stale_ = Stale();
        const juce::Rectangle<int> corner(0, 0, kRowHeader - 10, kColHeader - 8);
        if(painted_stale_) {
            g.setColour(colour::warn);
            g.setFont(juce::FontOptions(11.5f));
            g.drawFittedText(U(session_.LinkHealth() == Health::NoReplies ? "no answer from the device" : "no live data"),
                             corner, juce::Justification::bottomLeft, 2);
        } else if(session_.Connected()) {
            PaintAftertouch(g, corner);
        }
        for(int t = 0; t < int(mod::kTargets); ++t)
            PaintColumnHeader(g, t);
        for(int s = 0; s < int(mod::kSources); ++s)
            PaintRowHeader(g, s);
        for(int c = 0; c < kCells; ++c)
            PaintCell(g, c);
    }

    // ------------------------------------------------------------------ mouse
    void mouseDown(const juce::MouseEvent& e) override {
        const int cell = CellAt(e.getPosition());
        if(cell == kNoCell)
            return;
        Select(cell);
        if(e.mods.isPopupMenu()) {
            ShowMenu(cell);
            return;
        }
        drag_cell_ = model_.Allowed(cell) ? cell : kNoCell;
        drag_start_ = model_.Depth(cell);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(drag_cell_ == kNoCell)
            return;
        const int step = e.mods.isShiftDown() ? 8 : 2; // px per depth step
        const int next = drag_start_ - e.getDistanceFromDragStartY() / step;
        if(next != model_.Depth(drag_cell_))
            session_.EditDepth(drag_cell_, next);
    }
    void mouseUp(const juce::MouseEvent&) override { drag_cell_ = kNoCell; }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int cell = CellAt(e.getPosition());
        if(cell != kNoCell && model_.Allowed(cell) && model_.Depth(cell) != 0)
            session_.EditDepth(cell, model_.Depth(cell), !model_.Enabled(cell));
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const int cell = CellAt(e.getPosition());
        if(cell == kNoCell || !model_.Allowed(cell) || std::abs(w.deltaY) < 1e-6f)
            return;
        Select(cell);
        const int step = e.mods.isShiftDown() ? 10 : 1;
        session_.EditDepth(cell, model_.Depth(cell) + (w.deltaY > 0 ? step : -step));
    }

    // ------------------------------------------------------------------ keyboard
    bool keyPressed(const juce::KeyPress& key) override {
        const int c = model_.selected, s = SourceOf(c), t = TargetOf(c), d = model_.Depth(c);
        const int code = key.getKeyCode();
        const juce::juce_wchar ch = key.getTextCharacter();
        if(code == juce::KeyPress::upKey)
            Select(((s + 7) % 8) * 8 + t);
        else if(code == juce::KeyPress::downKey)
            Select(((s + 1) % 8) * 8 + t);
        else if(code == juce::KeyPress::leftKey)
            Select(s * 8 + (t + 7) % 8);
        else if(code == juce::KeyPress::rightKey)
            Select(s * 8 + (t + 1) % 8);
        else if(ch == '+' || ch == '=')
            session_.EditDepth(c, d + 1);
        else if(ch == '-')
            session_.EditDepth(c, d - 1);
        else if(code == juce::KeyPress::pageUpKey)
            session_.EditDepth(c, d + 10);
        else if(code == juce::KeyPress::pageDownKey)
            session_.EditDepth(c, d - 10);
        else if(ch == '0' || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
            session_.EditDepth(c, 0);
        else if(code == juce::KeyPress::spaceKey) {
            if(d != 0)
                session_.EditDepth(c, d, !model_.Enabled(c));
        } else
            return false;
        return true;
    }

    juce::String getTooltip() override {
        const juce::Point<int> p = getMouseXYRelative();
        const int cell = CellAt(p);
        if(cell == kNoCell && p.x < kRowHeader && p.y > kColHeader) {
            const int s = juce::jlimit(0, int(mod::kSources) - 1, (p.y - kColHeader) * int(mod::kSources)
                                                                     / std::max(1, getHeight() - kColHeader));
            switch(SourceMeter(s)) {
                case Meter::Idle: return U("No active connection: the source is not computed, the display rests.");
                case Meter::Fast: return U("Faster than the live display (20 frames/s): band instead of value. This "
                                           "firmware does not report the range it passed through.");
                case Meter::Stale: return U("No live data from the device. Is the port held by another application?");
                case Meter::Live: break;
            }
            juce::String tip = U(kSourceNames[s]) + ": " + juce::String(monitor_.source[s], 2);
            if(monitor_.has_range)
                tip << U(" (range of the last 50 ms ") << juce::String(monitor_.min[s], 2) << U(" … ")
                    << juce::String(monitor_.max[s], 2) << U(": band; line: current value)");
            if(s <= mod::Random && model_.SourceParam(s, mod::ParamSync) != mod::Free)
                tip << "\n" << U(ClockText(s));
            return tip;
        }
        if(cell == kNoCell && p.x < kRowHeader && p.y < kColHeader)
            return U(AftertouchTip());
        if(cell == kNoCell && p.y < kColHeader && p.x >= kRowHeader) {
            const int t = juce::jlimit(0, int(mod::kTargets) - 1, (p.x - kRowHeader) * int(mod::kTargets)
                                                                     / std::max(1, getWidth() - kRowHeader));
            const Column& col = model_.columns[std::size_t(t)];
            if(!col.present)
                return U("The instrument has no target here.");
            juce::String tip = U(col.name) + U(": depth 100 = ") + U(col.SpanLabel());
            const GoalSpan span = GoalSpanOf(model_, monitor_, std::size_t(t));
            if(has_monitor_ && span.has_base) {
                tip << U("\nBase value ") << juce::String(monitor_.base[t], 2)
                    << U(" (white line: pot, CC or fixed value of the instrument, before modulation)")
                    << U("\nTarget value now ") << juce::String(monitor_.Result(std::size_t(t)), 2) << U(", range ")
                    << juce::String(span.lo, 2) << U(" … ") << juce::String(span.hi, 2)
                    << (span.clipped_low || span.clipped_high ? U(" (red ends: limited at 0 or 1)") : U(""));
            } else if(has_monitor_) {
                tip << U("\nGlobal offset ") << juce::String(monitor_.offset[t], 2)
                    << U(" (the instrument reports no base value)");
            }
            if(has_monitor_)
                tip << U("\nTarget value before the smoothing in the instrument, global part only.");
            if(span.voice_part)
                tip << U(" Poly AT also acts per voice here and is not included.");
            if(col.latched)
                tip << U("\nTaken over at note-on: changes act from the next note on, sounding notes "
                         "stay as they were played.");
            return tip;
        }
        if(cell == kNoCell)
            return {};
        if(!model_.Allowed(cell))
            return U(model_.BlockedReason(cell));
        const Column& col = model_.columns[std::size_t(TargetOf(cell))];
        juce::String tip = U(kSourceNames[SourceOf(cell)]) + U(" → ") + U(col.name) + ": "
            + juce::String(model_.Depth(cell)) + (model_.Enabled(cell) ? "" : " (off)");
        if(SourceOf(cell) == mod::PolyAt)
            tip << U(col.per_voice ? " · Poly AT per voice" : " · Poly AT of the last played note");
        return tip;
    }

  private:
    void PaintColumnHeader(juce::Graphics& g, int t) {
        const Column& col = model_.columns[std::size_t(t)];
        const juce::Rectangle<int> cell = CellBounds(t);
        juce::Rectangle<int> r(cell.getX(), 0, cell.getWidth(), kColHeader - 6);
        g.setColour(col.present ? colour::ink : colour::muted);
        g.setFont(juce::FontOptions(13.0f, col.present ? juce::Font::bold : juce::Font::plain));
        g.drawText(col.present ? U(col.name) : U("—"), r.removeFromTop(20), juce::Justification::centred);
        g.setColour(colour::muted);
        g.setFont(juce::FontOptions(11.5f));
        // With a reported base value the header shows where the pot/CC stands; the span
        // (what depth 100 means) moves to the tooltip.
        const bool live = col.present && has_monitor_ && monitor_.HasBase(std::size_t(t));
        const juce::String span = !col.present ? U("not present")
                                               : (live ? U(col.ValueLabel(monitor_.base[t])) : U(col.SpanLabel()))
                                                     + (col.latched ? U(" · note-on") : U(""));
        g.drawText(span, r.removeFromTop(16), juce::Justification::centred);
        if(col.present && has_monitor_ && monitor_.HasBase(std::size_t(t)) && monitor_.has_range) {
            const GoalSpan goal = GoalSpanOf(model_, monitor_, std::size_t(t));
            PaintGoalMeter(g, r.removeFromTop(8).reduced(6, 2).toFloat(), monitor_.base[t], goal.lo, goal.hi,
                           monitor_.Result(std::size_t(t)), goal.clipped_low, goal.clipped_high, Stale());
        } else if(col.present && has_monitor_ && monitor_.HasBase(std::size_t(t)))
            PaintBaseMeter(g, r.removeFromTop(8).reduced(6, 2).toFloat(), monitor_.base[t],
                           monitor_.Result(std::size_t(t)), Stale());
        else if(col.present)
            PaintMeter(g, r.removeFromTop(8).reduced(6, 2).toFloat(), has_monitor_ ? monitor_.offset[t] : 0.0f, true,
                       Stale() ? Meter::Stale : Meter::Live);
    }

    void PaintRowHeader(juce::Graphics& g, int s) {
        const juce::Rectangle<int> cell = CellBounds(s * 8);
        juce::Rectangle<int> r(0, cell.getY(), kRowHeader - 10, cell.getHeight());
        r = r.withSizeKeepingCentre(r.getWidth(), 30);
        g.setColour(colour::ink);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        juce::Rectangle<int> title = r.removeFromTop(18);
        g.drawText(U(kSourceNames[s]), title, juce::Justification::centredLeft);
        // The kind the shared aftertouch display follows; both rows stay editable.
        if((s == mod::ChannelAt || s == mod::PolyAt) && !Stale() && ShownAftertouch() == (s == mod::ChannelAt ? 1 : 2)) {
            g.setColour(colour::accent);
            g.fillEllipse(float(title.getRight() - 8), float(title.getCentreY() - 3), 6.0f, 6.0f);
        }
        const juce::Rectangle<float> bar = r.removeFromTop(8).withTrimmedTop(2).toFloat();
        const Meter state = SourceMeter(s);
        if(has_monitor_ && monitor_.has_range)
            PaintSpanMeter(g, bar, monitor_.min[s], monitor_.max[s], monitor_.source[s], s < mod::ModWheel, state);
        else
            PaintMeter(g, bar, has_monitor_ ? monitor_.source[s] : 0.0f, s < mod::ModWheel, state);
        if(s <= mod::Random && session_.Connected() && session_.Device().ExtendedMonitor()
           && model_.SourceParam(s, mod::ParamSync) != mod::Free && !ClockRunning(s)) {
            g.setColour(colour::warn);
            g.setFont(juce::FontOptions(10.5f));
            g.drawText(U("no clock"), juce::Rectangle<int>(0, int(bar.getBottom()), kRowHeader - 10, 12),
                       juce::Justification::centredLeft);
        }
    }

    // ------------------------------------------------------------------ aftertouch, clock
    // 1 channel, 2 poly, 0 none or unknown (older firmware).
    int ShownAftertouch() const { return has_monitor_ && monitor_.has_range ? monitor_.at_kind : 0; }

    void PaintAftertouch(juce::Graphics& g, juce::Rectangle<int> r) {
        r = r.withTrimmedTop(r.getHeight() - 34);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.setColour(colour::ink);
        juce::Rectangle<int> line = r.removeFromTop(16);
        g.drawText(U("Aftertouch"), line, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(colour::muted);
        const bool known = has_monitor_ && monitor_.has_range;
        const int kind = ShownAftertouch();
        juce::String detail = !known ? U("channel/poly separate") : kind == 0 ? U("—")
            : kind == 1 ? U("Channel")
                        : U("Poly · ") + NoteName(monitor_.at_note);
        g.drawText(detail, line.withTrimmedLeft(66), juce::Justification::centredLeft);
        const bool fresh = known && kind != 0 && monitor_.at_age < 127;
        PaintMeter(g, r.removeFromTop(8).withTrimmedTop(2).toFloat(), float(known ? monitor_.at_value : 0) / 127.0f,
                   false, fresh ? Meter::Live : Meter::Idle);
    }

    static juce::String NoteName(int note) { // C4 = 60, as in most DAWs
        static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
    }

    std::string AftertouchTip() const {
        if(!has_monitor_ || !monitor_.has_range)
            return "Aftertouch: this firmware does not report which kind came last. Channel AT and Poly AT are "
                   "shown separately in their rows (limited display).";
        std::string tip = "Aftertouch follows the kind received last automatically (also at value 0). Channel AT and "
                          "Poly AT stay technically separate rows; the dot marks the one shown.";
        if(monitor_.at_kind == 2)
            tip += "\nPoly: the note addressed last, as long as it is held. Global targets keep using the "
                   "last played note (row Poly AT).";
        if(monitor_.at_age == 127)
            tip += "\nNo new aftertouch message for more than 2.5 s.";
        return tip;
    }

    bool ClockRunning(int s) const { // unknown counts as running: not claimed either way
        return !has_monitor_
            || monitor_.ClockState(model_.SourceParam(s, mod::ParamSync), model_.SourceParam(s, mod::ParamGate)) != 0;
    }
    std::string ClockText(int s) const {
        if(!has_monitor_ || !monitor_.has_range)
            return "Clock state unknown (firmware without the extended monitor).";
        const bool midi = model_.SourceParam(s, mod::ParamSync) == mod::MidiClock;
        return ClockRunning(s) ? (midi ? "MIDI clock running." : "Gate pulses arriving.")
                               : (midi ? "No MIDI clock: the LFO is stopped." : "No gate pulses: the LFO is stopped.");
    }

    Meter SourceMeter(int s) const {
        if(Stale())
            return Meter::Stale;
        bool used = false;
        for(int t = 0; t < int(mod::kTargets); ++t)
            used = used || model_.Active(s * 8 + t);
        if(!used)
            return Meter::Idle;
        if(has_monitor_ && monitor_.has_range)
            return Meter::Live; // the band shows the real range for every sync mode
        if(s <= mod::Random && model_.SourceParam(s, mod::ParamSync) == mod::Free
           && RateToHz(model_.SourceParam(s, mod::ParamRate)) > kFastHz)
            return Meter::Fast;
        return Meter::Live;
    }

    void PaintCell(juce::Graphics& g, int c) {
        const juce::Rectangle<float> r = CellBounds(c).toFloat();
        const bool ok = model_.Allowed(c), on = model_.Enabled(c);
        const int d = model_.Depth(c);
        if(!ok) {
            g.saveState();
            g.reduceClipRegion(r.toNearestInt());
            g.setColour(colour::hatch);
            for(float x = r.getX() - r.getHeight(); x < r.getRight(); x += 12.0f)
                g.drawLine(x, r.getBottom(), x + r.getHeight(), r.getY(), 5.0f);
            g.restoreState();
            g.setColour(colour::line);
            g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
            return;
        }
        if(on && d != 0) {
            const float a = 0.15f + 0.6f * float(std::abs(d)) / 100.0f; // opacity follows |depth|
            g.setColour((d > 0 ? colour::pos : colour::neg).withAlpha(a));
            g.fillRoundedRectangle(r, 7.0f);
        }
        g.setColour(colour::line);
        if(d != 0 && !on) {
            juce::Path p;
            p.addRoundedRectangle(r.reduced(0.5f), 7.0f);
            const float dash[] = {4.0f, 3.0f};
            juce::Path dashed;
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, p, dash, 2);
            g.setColour(colour::muted);
            g.fillPath(dashed);
        } else {
            g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
        }
        juce::String text = d == 0 ? U("·") : (d > 0 ? "+" : "") + juce::String(d);
        if(d != 0 && !on)
            text = "(" + text + ")";
        g.setColour(d == 0 ? colour::muted : colour::ink);
        g.setFont(juce::FontOptions(14.0f, d == 0 ? juce::Font::plain : juce::Font::bold));
        g.drawText(text, r, juce::Justification::centred);
        if(c == model_.selected) {
            g.setColour(colour::select);
            g.drawRoundedRectangle(r.expanded(1.5f), 8.0f, 2.0f);
        }
        if(session_.Pending(c)) { // not yet confirmed by the device
            g.setColour(colour::warn);
            g.fillEllipse(r.getRight() - 10, r.getY() + 4, 6, 6);
        }
    }

    void ShowMenu(int cell) {
        if(!model_.Allowed(cell))
            return;
        juce::PopupMenu m;
        m.addItem(1, "Set to 0");
        m.addItem(2, "Invert", model_.Depth(cell) != 0);
        m.addItem(3, model_.Enabled(cell) ? "Switch off" : "Switch on", model_.Depth(cell) != 0);
        m.setLookAndFeel(&getLookAndFeel()); // the app's colours, not the platform default
        // At the mouse pointer, as context menus open everywhere else.
        m.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<MatrixGrid>(this), cell](int id) {
                            if(!safe)
                                return;
                            MatrixGrid& self = *safe;
                            const int d = self.model_.Depth(cell);
                            if(id == 1)
                                self.session_.EditDepth(cell, 0);
                            else if(id == 2)
                                self.session_.EditDepth(cell, -d);
                            else if(id == 3)
                                self.session_.EditDepth(cell, d, !self.model_.Enabled(cell));
                        });
    }

    Model& model_;
    Session& session_;
    MonitorValues monitor_;
    bool has_monitor_ = false, painted_stale_ = false;
    int drag_cell_ = kNoCell, drag_start_ = 0;
};
}
