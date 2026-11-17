#include "FilterPanel.h"
#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

namespace
{
// One tick box: square, label and count.
class Tick : public Component
{
public:
    Tick (Category c, String o) : cat (c), option (std::move (o)) { setRepaintsOnMouseActivity (true); }
    void set (bool on, int n) { if (on != ticked || n != count) { ticked = on; count = n; repaint(); } }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (isMouseOver()) { g.setColour (colours::panelHi); g.fillRoundedRectangle (r, 3); }
        const bool empty = count == 0 && ! ticked;
        auto box = Rectangle<float> (r.getX() + 4, r.getCentreY() - 6, 12, 12);
        g.setColour (ticked ? colours::accent : colours::line);
        if (ticked)
        {
            g.fillRoundedRectangle (box, 2.5f);
            Path t; t.startNewSubPath (box.getX() + 2.6f, box.getCentreY());
            t.lineTo (box.getX() + 5.0f, box.getBottom() - 3.0f); t.lineTo (box.getRight() - 2.4f, box.getY() + 3.0f);
            g.setColour (Colours::black); g.strokePath (t, PathStrokeType (1.7f));
        }
        else g.drawRoundedRectangle (box, 2.5f, 1.2f);
        g.setFont (FontOptions (12.5f));
        g.setColour (empty ? colours::textDim.withAlpha (0.6f) : colours::text);
        auto textR = r.withTrimmedLeft (21).withTrimmedRight (4);
        auto countStr = count > 0 ? String (count) : String();
        g.drawText (option, textR.withTrimmedRight (countStr.length() * 7.0f + 2), Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.setFont (FontOptions (11.0f));
        g.drawText (countStr, textR, Justification::centredRight, false);
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onClick) onClick();
    }
    std::function<void()> onClick;
    Category cat;
    String option;
    bool ticked = false;
    int count = 0;
};

// Key grid tick: compact cell
class KeyCell : public Tick
{
public:
    using Tick::Tick;
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool empty = count == 0 && ! ticked;
        g.setColour (ticked ? colours::accent : (isMouseOver() ? colours::panelHi.brighter (0.1f) : colours::panelHi));
        g.fillRoundedRectangle (r, 3);
        g.setColour (ticked ? Colours::black : (empty ? colours::textDim.withAlpha (0.5f) : colours::text));
        g.setFont (FontOptions (12.0f, Font::bold));
        auto label = option.upToFirstOccurrenceOf (" ", false, false);
        g.drawText (label, r.withTrimmedBottom (r.getHeight() * 0.35f), Justification::centredBottom);
        g.setFont (FontOptions (9.5f));
        g.setColour (ticked ? Colours::black.withAlpha (0.7f) : colours::textDim);
        g.drawText (count > 0 ? String (count) : String ("-"), r.withTrimmedTop (r.getHeight() * 0.62f), Justification::centredTop);
    }
};

class Header : public Component
{
public:
    Header (Category c) : cat (c) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::textDim);
        g.setFont (FontOptions (11.5f, Font::bold));
        Path arrow;
        float ax = 8, ay = r.getCentreY();
        if (collapsed) arrow.addTriangle (ax - 3, ay - 4, ax - 3, ay + 4, ax + 3, ay);
        else arrow.addTriangle (ax - 4, ay - 2.5f, ax + 4, ay - 2.5f, ax, ay + 3.5f);
        g.fillPath (arrow);
        g.setColour (ticks > 0 ? colours::accent : colours::text);
        g.drawText (categoryName (cat).toUpperCase() + (ticks > 0 ? "  (" + String (ticks) + ")" : String()),
                    r.withTrimmedLeft (18), Justification::centredLeft);
        if (ticks > 0)
        {
            g.setColour (colours::textDim);
            g.setFont (FontOptions (11.0f));
            g.drawText ("clear", r.withTrimmedRight (4), Justification::centredRight);
        }
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        if (ticks > 0 && e.x > getWidth() - 40) { if (onClear) onClear(); return; }
        collapsed = ! collapsed;
        if (onCollapse) onCollapse();
    }
    std::function<void()> onClear, onCollapse;
    Category cat;
    bool collapsed = false;
    int ticks = 0;
};
}

class FilterPanel::Content : public Component
{
public:
    explicit Content (FilterPanel& o) : owner (o)
    {
        compatible.setButtonText ("+ compatible");
        compatible.setTooltip ("Also show relative major/minor keys, and single-note sounds that fit the scale");
        compatible.onClick = [this] { if (owner.onCompatibleToggled) owner.onCompatibleToggled(); };
        addAndMakeVisible (compatible);
        for (int c = 0; c < (int) Category::Count; ++c)
        {
            auto h = std::make_unique<Header> ((Category) c);
            h->onClear = [this, c] { if (owner.onClearCategory) owner.onClearCategory ((Category) c); };
            h->onCollapse = [this] { layout(); };
            // long, rarely used groups start folded
            if (c == (int) Category::Register || c == (int) Category::Length || c == (int) Category::Source)
                h->collapsed = true;
            addAndMakeVisible (*h);
            headers.push_back (std::move (h));
        }
    }

    void update (const FilterModel& model, const FilterModel::Result& res, const FilterState& st)
    {
        compatible.setToggleState (st.compatibleKeys, dontSendNotification);
        for (int c = 0; c < (int) Category::Count; ++c)
        {
            auto& opts = model.options ((Category) c);
            auto& list = ticks[(size_t) c];
            // rebuild the boxes when the option list changes
            bool same = (int) list.size() == opts.size();
            for (size_t i = 0; same && i < list.size(); ++i) same = list[i]->option == opts[(int) i];
            if (! same)
            {
                list.clear();
                for (auto& o : opts)
                {
                    std::unique_ptr<Tick> t (c == (int) Category::Key && o != "No key" ? new KeyCell ((Category) c, o)
                                                                                       : new Tick ((Category) c, o));
                    t->onClick = [this, cat = (Category) c, o] { if (owner.onToggle) owner.onToggle (cat, o); };
                    addAndMakeVisible (*t);
                    list.push_back (std::move (t));
                }
            }
            int tickedCount = 0;
            for (size_t i = 0; i < list.size(); ++i)
            {
                bool on = st.isTicked ((Category) c, list[i]->option);
                tickedCount += on ? 1 : 0;
                list[i]->set (on, i < res.counts[(size_t) c].size() ? res.counts[(size_t) c][i] : 0);
            }
            headers[(size_t) c]->ticks = tickedCount;
            headers[(size_t) c]->repaint();
        }
        layout();
    }

    void layout()
    {
        const int w = getWidth() > 0 ? getWidth() : 240;
        int y = 4;
        for (int c = 0; c < (int) Category::Count; ++c)
        {
            auto& h = *headers[(size_t) c];
            auto& list = ticks[(size_t) c];
            const bool isKey = c == (int) Category::Key;
            const bool emptyDynamic = list.empty();
            h.setVisible (true);
            h.setBounds (0, y, w, 22);
            y += 24;
            if (h.collapsed || emptyDynamic)
            {
                for (auto& t : list) t->setVisible (false);
                if (isKey) compatible.setVisible (false);
                if (emptyDynamic && ! h.collapsed)
                {
                    // nothing to show yet (no styles/tags found)
                }
                y += 4;
                continue;
            }
            if (isKey)
            {
                // 12 x 2 grid (major row, minor row), then "No key"
                const int cell = (w - 8) / 12;
                for (size_t i = 0; i < list.size(); ++i)
                {
                    auto& t = list[i];
                    t->setVisible (true);
                    if (i < 24)
                    {
                        int row = (int) i / 12, col = (int) i % 12;
                        t->setBounds (4 + col * cell, y + row * 34, cell, 34);
                    }
                    else t->setBounds (4, y + 70, w / 2 - 6, 20);
                }
                compatible.setVisible (true);
                compatible.setBounds (w / 2, y + 70, w / 2 - 4, 20);
                y += 70 + 24;
            }
            else
            {
                const bool twoCol = c != (int) Category::Source && c != (int) Category::Style && c != (int) Category::Tags;
                const int colW = twoCol ? (w - 8) / 2 : w - 8;
                int i = 0;
                for (auto& t : list)
                {
                    t->setVisible (true);
                    int col = twoCol ? i % 2 : 0, row = twoCol ? i / 2 : i;
                    t->setBounds (4 + col * colW, y + row * 21, colW, 21);
                    ++i;
                }
                int rows = twoCol ? (i + 1) / 2 : i;
                y += rows * 21 + 6;
            }
        }
        setSize (w, y + 8);
    }

    void resized() override {}

    FilterPanel& owner;
    juce::ToggleButton compatible;
    std::vector<std::unique_ptr<Header>> headers;
    std::array<std::vector<std::unique_ptr<Tick>>, (size_t) Category::Count> ticks;
};

FilterPanel::FilterPanel()
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    addAndMakeVisible (clearAll);
    addAndMakeVisible (favourites);
    addAndMakeVisible (countLabel);
    countLabel.setColour (Label::textColourId, colours::textDim);
    countLabel.setFont (FontOptions (11.5f));
    clearAll.onClick = [this] { if (onClearAll) onClearAll(); };
    favourites.onClick = [this] { if (onFavouritesToggled) onFavouritesToggled(); };
}

FilterPanel::~FilterPanel() = default;

void FilterPanel::update (const FilterModel& model, const FilterModel::Result& result, const FilterState& state, int total)
{
    favourites.setToggleState (state.favouritesOnly, dontSendNotification);
    countLabel.setText (String ((int) result.indices.size()) + " of " + String (total) + " sounds", dontSendNotification);
    content->setSize (viewport.getMaximumVisibleWidth(), content->getHeight());
    content->update (model, result, state);
}

void FilterPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (28).reduced (4, 3);
    clearAll.setBounds (top.removeFromRight (70));
    favourites.setBounds (top);
    countLabel.setBounds (r.removeFromTop (18).withTrimmedLeft (6));
    viewport.setBounds (r);
    content->setSize (viewport.getMaximumVisibleWidth(), content->getHeight());
    content->layout();
}

void FilterPanel::paint (Graphics& g)
{
    g.fillAll (colours::panel);
}
}
