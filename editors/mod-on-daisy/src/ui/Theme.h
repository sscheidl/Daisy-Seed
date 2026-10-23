#pragma once
// Colours and helpers shared by all editor components. Dark, like a studio
// application; the palette follows the browser editor's dark scheme.
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

namespace editor::ui {
namespace colour {
const juce::Colour bg{0xff131315}, panel{0xff1c1c1f}, raised{0xff232327}, ink{0xffececef}, muted{0xff9a9aa3},
    line{0xff303036}, accent{0xfff08a4b}, pos{0xfff08a4b}, neg{0xff5aa0f5}, select{0xffffffff}, hatch{0xff2a2a30},
    ok{0xff66bb6a}, warn{0xffe0a030}, err{0xffef5350};
}

// The core works in UTF-8 std::string; JUCE asserts on non-ASCII char* literals.
inline juce::String U(const char* utf8) { return juce::String::fromUTF8(utf8); }
inline juce::String U(const std::string& utf8) { return juce::String::fromUTF8(utf8.c_str()); }

class LookAndFeel : public juce::LookAndFeel_V4 {
  public:
    LookAndFeel() {
        setColourScheme({colour::panel, colour::bg, colour::raised, colour::line, colour::ink, colour::accent,
                         juce::Colours::white, colour::raised, colour::ink});
        setColour(juce::ResizableWindow::backgroundColourId, colour::bg);
        setColour(juce::ComboBox::backgroundColourId, colour::raised);
        setColour(juce::ComboBox::outlineColourId, colour::line);
        setColour(juce::TextButton::buttonColourId, colour::raised);
        setColour(juce::TextButton::buttonOnColourId, colour::accent);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::Slider::thumbColourId, colour::accent);
        setColour(juce::Slider::trackColourId, colour::accent.withAlpha(0.6f));
        setColour(juce::Slider::backgroundColourId, colour::line);
        setColour(juce::Slider::textBoxOutlineColourId, colour::line);
        setColour(juce::Label::textColourId, colour::ink);
        setColour(juce::TextEditor::backgroundColourId, colour::raised);
        setColour(juce::TextEditor::outlineColourId, colour::line);
        setColour(juce::ToggleButton::tickColourId, colour::accent);
        setColour(juce::PopupMenu::backgroundColourId, colour::raised);
        setColour(juce::TooltipWindow::backgroundColourId, colour::raised);
        setColour(juce::TooltipWindow::outlineColourId, colour::line);
    }
};

// A titled card behind a group of controls.
inline void PaintCard(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) {
    g.setColour(colour::panel);
    g.fillRoundedRectangle(r.toFloat(), 10.0f);
    g.setColour(colour::line);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
    if(title.isNotEmpty()) {
        g.setColour(colour::muted);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(title.toUpperCase(), r.reduced(14, 10).removeFromTop(16), juce::Justification::centredLeft);
    }
}

// Horizontal meter: bipolar from the centre or unipolar from the left.
inline void PaintMeter(juce::Graphics& g, juce::Rectangle<float> r, float value, bool bipolar) {
    value = juce::jlimit(-1.0f, 1.0f, value);
    g.setColour(colour::line);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(colour::accent);
    if(bipolar) {
        const float mid = r.getCentreX(), x = mid + value * r.getWidth() * 0.5f;
        g.fillRect(juce::Rectangle<float>(std::min(mid, x), r.getY(), std::abs(x - mid), r.getHeight()));
        g.setColour(colour::muted);
        g.fillRect(juce::Rectangle<float>(mid - 0.5f, r.getY() - 1, 1, r.getHeight() + 2));
    } else {
        g.fillRect(r.withWidth(r.getWidth() * std::max(0.0f, value)));
    }
}
}
