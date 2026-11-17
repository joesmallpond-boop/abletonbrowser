#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sb
{
namespace colours
{
    const juce::Colour bg        { 0xff1b1c1e };
    const juce::Colour panel     { 0xff242528 };
    const juce::Colour panelHi   { 0xff2e2f33 };
    const juce::Colour line      { 0xff3a3b40 };
    const juce::Colour text      { 0xffd8d8d8 };
    const juce::Colour textDim   { 0xff8b8c90 };
    const juce::Colour accent    { 0xffff9f43 };  // orange
    const juce::Colour accent2   { 0xff4fc3f7 };  // blue (playheads)
    const juce::Colour good      { 0xff7bd88f };
    const juce::Colour wave      { 0xffb9bdc4 };
}

class BrowserLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrowserLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.5f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.5f); }
    juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (12.5f); }
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int id,
                                int w, int h, bool over, bool down, int flags) override;
};
}
