#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sb
{
// Colours taken from Live 12's "Default Dark Neutral Medium" theme so the plugin sits in Live
// like a native device.
namespace colours
{
    const juce::Colour bg        { 0xff2a2a2a };  // Desktop (gaps between panels)
    const juce::Colour panel     { 0xff363636 };  // SurfaceBackground
    const juce::Colour panelHi   { 0xff464646 };  // SurfaceHighlight / column heads
    const juce::Colour area      { 0xff242424 };  // SurfaceArea (browser list)
    const juce::Colour field     { 0xff1e1e1e };  // ControlBackground
    const juce::Colour display   { 0xff181818 };  // DisplayBackground (waveform)
    const juce::Colour line      { 0xff111111 };  // ControlContrastFrame
    const juce::Colour divider   { 0xff2a2a2a };
    const juce::Colour handle    { 0xff5d5d5d };  // ControlFillHandle
    const juce::Colour text      { 0xffb5b5b5 };  // ControlForeground
    const juce::Colour textDim   { 0xff757575 };  // TextDisabled
    const juce::Colour onText    { 0xff070707 };  // ControlOnForeground
    const juce::Colour accent    { 0xffffad56 };  // ChosenDefault (orange)
    const juce::Colour accent2   { 0xff03c3d5 };  // RangeDefault (cyan dials)
    const juce::Colour good      { 0xff00d38d };  // ChosenPlay
    const juce::Colour prelisten { 0xff3c6ab6 };  // ChosenPreListen (browser headphones)
    const juce::Colour selection { 0xffb0ddeb };  // SelectionBackground
    const juce::Colour selectionStandby { 0xff637e86 };
    const juce::Colour wave      { 0xff868686 };  // BrowserSampleWaveform
    const juce::Colour pianoWhite { 0xff868686 };
    const juce::Colour pianoBlack { 0xff464646 };
    const juce::Colour keyZone   { 0xff28bd56 };
    const juce::Colour clipBlue  { 0xff6baace };  // Clip colour 7
}

inline juce::Font liveFont (float size = 11.0f, bool bold = false)
{
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
}

class BrowserLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrowserLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return liveFont(); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return liveFont(); }
    juce::Font getLabelFont (juce::Label&) override { return liveFont(); }
    juce::Font getPopupMenuFont() override { return liveFont (12.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int id,
                                int w, int h, bool over, bool down, int flags) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
};
}
