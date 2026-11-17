#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

BrowserLookAndFeel::BrowserLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::bg);
    setColour (Label::textColourId, colours::text);
    setColour (TextButton::buttonColourId, colours::panelHi);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (ComboBox::backgroundColourId, colours::panelHi);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::line);
    setColour (ComboBox::arrowColourId, colours::textDim);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (TextEditor::backgroundColourId, colours::bg);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, colours::line);
    setColour (TextEditor::focusedOutlineColourId, colours::accent);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, colours::accent);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::rotarySliderFillColourId, colours::accent);
    setColour (Slider::trackColourId, colours::accent);
    setColour (Slider::backgroundColourId, colours::line);
    setColour (Slider::thumbColourId, colours::text);
    setColour (ListBox::backgroundColourId, colours::panel);
    setColour (ScrollBar::thumbColourId, colours::line.brighter (0.2f));
    setColour (TableHeaderComponent::backgroundColourId, colours::panelHi);
    setColour (TableHeaderComponent::textColourId, colours::textDim);
    setColour (TableHeaderComponent::outlineColourId, colours::line);
    setColour (ToggleButton::textColourId, colours::text);
    setColour (ToggleButton::tickColourId, colours::accent);
    setColour (ToggleButton::tickDisabledColourId, colours::textDim);
    setColour (TooltipWindow::backgroundColourId, colours::panelHi);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, colours::accent);
}

void BrowserLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
    auto r = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto c = bounds.getCentre();
    auto angle = a0 + pos * (a1 - a0);
    const float thick = jmax (2.0f, r * 0.18f);

    Path track;
    track.addCentredArc (c.x, c.y, r - thick, r - thick, 0, a0, a1, true);
    g.setColour (colours::line);
    g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    float from = a0;
    if (bipolar) from = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
    Path val;
    val.addCentredArc (c.x, c.y, r - thick, r - thick, 0, jmin (from, angle), jmax (from, angle), true);
    g.setColour (s.isEnabled() ? colours::accent : colours::textDim);
    g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    Path needle;
    needle.addRoundedRectangle (-1.2f, -(r - thick * 2.2f), 2.4f, (r - thick * 2.2f) * 0.6f, 1.0f);
    g.setColour (colours::text);
    g.fillPath (needle, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void BrowserLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float box = jmin (13.0f, r.getHeight() - 4);
    auto boxR = Rectangle<float> (r.getX() + 2, r.getCentreY() - box / 2, box, box);
    g.setColour (on ? colours::accent : (over ? colours::line.brighter (0.3f) : colours::line));
    if (on) g.fillRoundedRectangle (boxR, 2.5f);
    else g.drawRoundedRectangle (boxR, 2.5f, 1.2f);
    if (on)
    {
        Path tick;
        tick.startNewSubPath (boxR.getX() + box * 0.22f, boxR.getCentreY());
        tick.lineTo (boxR.getX() + box * 0.42f, boxR.getBottom() - box * 0.25f);
        tick.lineTo (boxR.getRight() - box * 0.2f, boxR.getY() + box * 0.25f);
        g.setColour (Colours::black);
        g.strokePath (tick, PathStrokeType (1.8f));
    }
    g.setColour (b.isEnabled() ? colours::text : colours::textDim);
    g.setFont (FontOptions (12.5f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (box + 7), Justification::centredLeft, true);
}

void BrowserLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : base;
    if (down) c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void BrowserLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (false) ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    Path arrow;
    float ax = (float) w - 12, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (colours::textDim);
    g.fillPath (arrow);
}

void BrowserLookAndFeel::drawTableHeaderBackground (Graphics& g, TableHeaderComponent& h)
{
    g.fillAll (colours::panelHi);
    g.setColour (colours::line);
    g.fillRect (0, h.getHeight() - 1, h.getWidth(), 1);
}

void BrowserLookAndFeel::drawTableHeaderColumn (Graphics& g, TableHeaderComponent&, const String& name, int,
                                                int w, int h, bool over, bool, int flags)
{
    if (over) g.fillAll (colours::line.withAlpha (0.4f));
    g.setColour (colours::line);
    g.fillRect (w - 1, 4, 1, h - 8);
    auto area = Rectangle<int> (6, 0, w - 10, h);
    if ((flags & (TableHeaderComponent::sortedForwards | TableHeaderComponent::sortedBackwards)) != 0)
    {
        Path p;
        const bool fwd = (flags & TableHeaderComponent::sortedForwards) != 0;
        float cx = (float) w - 10, cy = h * 0.5f;
        if (fwd) p.addTriangle (cx - 3.5f, cy + 2, cx + 3.5f, cy + 2, cx, cy - 3);
        else p.addTriangle (cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 3);
        g.setColour (colours::accent);
        g.fillPath (p);
        area.removeFromRight (12);
    }
    g.setColour (colours::textDim);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawFittedText (name, area, Justification::centredLeft, 1);
}
}
