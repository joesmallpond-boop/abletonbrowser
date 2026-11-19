#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

BrowserLookAndFeel::BrowserLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::bg);
    setColour (Label::textColourId, colours::text);
    setColour (TextButton::buttonColourId, colours::field);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::onText);
    setColour (ComboBox::backgroundColourId, colours::field);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::line);
    setColour (ComboBox::arrowColourId, colours::text);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::headerTextColourId, colours::textDim);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::selection);
    setColour (PopupMenu::highlightedTextColourId, colours::onText);
    setColour (TextEditor::backgroundColourId, colours::field);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, colours::line);
    setColour (TextEditor::focusedOutlineColourId, colours::textDim);
    setColour (TextEditor::highlightColourId, colours::selection.withAlpha (0.6f));
    setColour (TextEditor::highlightedTextColourId, colours::onText);
    setColour (CaretComponent::caretColourId, colours::text);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, colours::field);
    setColour (Slider::rotarySliderFillColourId, colours::accent2);
    setColour (Slider::trackColourId, colours::accent2);
    setColour (Slider::backgroundColourId, colours::field);
    setColour (Slider::thumbColourId, colours::text);
    setColour (ListBox::backgroundColourId, colours::area);
    setColour (ListBox::outlineColourId, colours::line);
    setColour (ScrollBar::thumbColourId, Colour (0xff696969));
    setColour (TableHeaderComponent::backgroundColourId, colours::panelHi);
    setColour (TableHeaderComponent::textColourId, colours::text);
    setColour (TableHeaderComponent::outlineColourId, colours::divider);
    setColour (ToggleButton::textColourId, colours::text);
    setColour (ToggleButton::tickColourId, colours::accent);
    setColour (ToggleButton::tickDisabledColourId, colours::textDim);
    setColour (TooltipWindow::backgroundColourId, colours::panelHi);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::line);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, colours::selection);
    setColour (DirectoryContentsDisplayComponent::highlightedTextColourId, colours::onText);
    setColour (DirectoryContentsDisplayComponent::textColourId, colours::text);
}

// Live's dial: thin dark ring, cyan value arc, light needle from the centre.
void BrowserLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float r = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float thick = jlimit (2.0f, 3.0f, r * 0.14f);
    const bool on = s.isEnabled();

    Path track;
    track.addCentredArc (c.x, c.y, r - thick * 0.5f, r - thick * 0.5f, 0, a0, a1, true);
    g.setColour (colours::field);
    g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::butt));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    float from = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    Path val;
    val.addCentredArc (c.x, c.y, r - thick * 0.5f, r - thick * 0.5f, 0, jmin (from, angle), jmax (from, angle), true);
    g.setColour (on ? colours::accent2 : colours::textDim);
    g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::butt));

    const float len = r - thick - 1.5f;
    Point<float> tip (c.x + len * std::sin (angle), c.y - len * std::cos (angle));
    g.setColour (on ? colours::text : colours::textDim);
    g.drawLine ({ c, tip }, 1.6f);
}

void BrowserLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                           Slider::SliderStyle style, Slider& s)
{
    if (style == Slider::LinearBar || style == Slider::LinearBarVertical)
    {
        g.setColour (colours::field);
        g.fillRect (x, y, w, h);
        g.setColour (s.isEnabled() ? colours::accent2.withAlpha (0.35f) : colours::panelHi);
        g.fillRect ((float) x, (float) y, pos - (float) x, (float) h);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

// Square tick box (used for the filter ticks and a few options)
void BrowserLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float box = 11.0f;
    auto boxR = Rectangle<float> (r.getX() + 2, r.getCentreY() - box / 2, box, box);
    g.setColour (on ? colours::accent : colours::field);
    g.fillRect (boxR);
    g.setColour (over ? colours::textDim : colours::line);
    g.drawRect (boxR, 1.0f);
    if (on)
    {
        Path tick;
        tick.startNewSubPath (boxR.getX() + 2.4f, boxR.getCentreY());
        tick.lineTo (boxR.getX() + 4.6f, boxR.getBottom() - 2.6f);
        tick.lineTo (boxR.getRight() - 2.2f, boxR.getY() + 2.6f);
        g.setColour (colours::onText);
        g.strokePath (tick, PathStrokeType (1.5f));
    }
    g.setColour (b.isEnabled() ? colours::text : colours::textDim);
    g.setFont (liveFont());
    g.drawText (b.getButtonText(), r.withTrimmedLeft (box + 7), Justification::centredLeft, true);
}

// Live buttons: flat rectangles, orange when on.
void BrowserLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    Colour c = on ? b.findColour (TextButton::buttonOnColourId) : base;
    if (! b.isEnabled()) c = on ? colours::panelHi : colours::field.withAlpha (0.6f);
    else if (down) c = c.brighter (0.12f);
    else if (over) c = c.brighter (0.05f);
    g.setColour (c);
    g.fillRect (r);
    g.setColour (colours::line);
    g.drawRect (r, 1.0f);
}

void BrowserLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setFont (liveFont());
    g.setColour (! b.isEnabled() ? colours::textDim
                                 : on ? b.findColour (TextButton::textColourOnId) : b.findColour (TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), Justification::centred, 1, 0.8f);
}

void BrowserLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRect (r);
    g.setColour (colours::line);
    g.drawRect (r, 1.0f);
    Path arrow;
    const float ax = (float) w - 8, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 1.8f, ax + 3.5f, ay - 1.8f, ax, ay + 2.2f);
    g.setColour (box.isEnabled() ? colours::text : colours::textDim);
    g.fillPath (arrow);
}

void BrowserLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (2, 0, box.getWidth() - 14, box.getHeight());
    label.setFont (liveFont());
}

void BrowserLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

void BrowserLookAndFeel::fillTextEditorBackground (Graphics& g, int, int, TextEditor& e)
{
    g.fillAll (e.findColour (TextEditor::backgroundColourId));
}

void BrowserLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? colours::textDim : colours::line);
    g.drawRect (0, 0, w, h);
}

void BrowserLookAndFeel::drawTableHeaderBackground (Graphics& g, TableHeaderComponent& h)
{
    g.fillAll (colours::panelHi);
    g.setColour (colours::divider);
    g.fillRect (0, h.getHeight() - 1, h.getWidth(), 1);
}

void BrowserLookAndFeel::drawTableHeaderColumn (Graphics& g, TableHeaderComponent&, const String& name, int,
                                                int w, int h, bool over, bool, int flags)
{
    if (over) g.fillAll (colours::panelHi.brighter (0.05f));
    g.setColour (colours::divider);
    g.fillRect (w - 1, 0, 1, h);
    auto area = Rectangle<int> (5, 0, w - 8, h);
    if ((flags & (TableHeaderComponent::sortedForwards | TableHeaderComponent::sortedBackwards)) != 0)
    {
        Path p;
        const bool fwd = (flags & TableHeaderComponent::sortedForwards) != 0;
        const float cx = (float) w - 9, cy = h * 0.5f;
        if (fwd) p.addTriangle (cx - 3, cy + 1.5f, cx + 3, cy + 1.5f, cx, cy - 2.5f);
        else p.addTriangle (cx - 3, cy - 1.5f, cx + 3, cy - 1.5f, cx, cy + 2.5f);
        g.setColour (colours::text);
        g.fillPath (p);
        area.removeFromRight (10);
    }
    g.setColour (colours::text);
    g.setFont (liveFont());
    g.drawFittedText (name, area, Justification::centredLeft, 1);
}

void BrowserLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical,
                                        int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour (colours::area);
    g.fillRect (x, y, w, h);
    Rectangle<int> thumb = vertical ? Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                                    : Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over || down ? Colour (0xff757575) : Colour (0xff696969));
    g.fillRect (thumb);
}

void BrowserLookAndFeel::drawCallOutBoxBackground (CallOutBox&, Graphics& g, const Path& path, Image&)
{
    g.setColour (colours::panel);
    g.fillPath (path);
    g.setColour (colours::line);
    g.strokePath (path, PathStrokeType (1.0f));
}

void BrowserLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (colours::panelHi);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (liveFont());
    g.drawFittedText (text, Rectangle<int> (0, 0, w, h).reduced (5, 3), Justification::centredLeft, 6);
}
}
