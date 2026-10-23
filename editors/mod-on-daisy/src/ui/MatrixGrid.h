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
    static constexpr uint32_t kStaleMs = 1500; // monitor frames come at 20 Hz
    // Above a quarter of the monitor rate a sampled LFO looks frozen (20 Hz sampled at
    // 20 Hz shows one value), so the meter shows a band instead.
    static constexpr float kFastHz = float(Session::kMonitorRate) / 4.0f;

    std::function<void(int cell)> selected; // selection changed

    MatrixGrid(Model& model, Session& session) : model_(model), session_(session) {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
    }

    void SetMonitor(const MonitorValues& m) {
        monitor_ = m;
        has_monitor_ = true;
        last_monitor_ms_ = juce::Time::getMillisecondCounter();
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
    bool Stale() const {
        return session_.Connected() && (!has_monitor_ || juce::Time::getMillisecondCounter() - last_monitor_ms_ > kStaleMs);
    }

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
        if(painted_stale_) {
            g.setColour(colour::warn);
            g.setFont(juce::FontOptions(11.5f));
            g.drawFittedText(U("keine Live-Daten"), juce::Rectangle<int>(0, 0, kRowHeader - 8, kColHeader - 8),
                             juce::Justification::bottomLeft, 2);
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
                case Meter::Idle: return U("Keine aktive Verbindung: die Quelle wird nicht berechnet, die Anzeige ruht.");
                case Meter::Fast: return U("Schneller als die Live-Anzeige (20 Bilder/s): Band statt Wert.");
                case Meter::Stale: return U("Keine Live-Daten vom Gerät. Ist der Port von einer anderen Anwendung belegt?");
                case Meter::Live: return U(kSourceNames[s]) + ": " + juce::String(monitor_.source[s], 2);
            }
        }
        if(cell == kNoCell)
            return {};
        if(!model_.Allowed(cell))
            return U(model_.BlockedReason(cell));
        const Column& col = model_.columns[std::size_t(TargetOf(cell))];
        juce::String tip = U(kSourceNames[SourceOf(cell)]) + U(" → ") + U(col.name) + ": "
            + juce::String(model_.Depth(cell)) + (model_.Enabled(cell) ? "" : " (aus)");
        if(SourceOf(cell) == mod::PolyAt)
            tip << U(col.per_voice ? " · Poly AT pro Stimme" : " · Poly AT der zuletzt gespielten Note");
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
        const juce::String span = col.present ? U(col.SpanLabel()) + (col.latched ? U(" · Anschlag") : U(""))
                                              : U("nicht vorhanden");
        g.drawText(span, r.removeFromTop(16), juce::Justification::centred);
        if(col.present)
            PaintMeter(g, r.removeFromTop(8).reduced(6, 2).toFloat(), has_monitor_ ? monitor_.offset[t] : 0.0f, true,
                       Stale() ? Meter::Stale : Meter::Live);
    }

    void PaintRowHeader(juce::Graphics& g, int s) {
        const juce::Rectangle<int> cell = CellBounds(s * 8);
        juce::Rectangle<int> r(0, cell.getY(), kRowHeader - 10, cell.getHeight());
        r = r.withSizeKeepingCentre(r.getWidth(), 30);
        g.setColour(colour::ink);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(U(kSourceNames[s]), r.removeFromTop(18), juce::Justification::centredLeft);
        PaintMeter(g, r.removeFromTop(8).withTrimmedTop(2).toFloat(), has_monitor_ ? monitor_.source[s] : 0.0f,
                   s < mod::ModWheel, SourceMeter(s));
    }

    Meter SourceMeter(int s) const {
        if(Stale())
            return Meter::Stale;
        bool used = false;
        for(int t = 0; t < int(mod::kTargets); ++t)
            used = used || model_.Active(s * 8 + t);
        if(!used)
            return Meter::Idle;
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
        m.addItem(1, "Auf 0 setzen");
        m.addItem(2, "Invertieren", model_.Depth(cell) != 0);
        m.addItem(3, model_.Enabled(cell) ? "Ausschalten" : "Einschalten", model_.Depth(cell) != 0);
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
    uint32_t last_monitor_ms_ = 0;
    int drag_cell_ = kNoCell, drag_start_ = 0;
};
}
