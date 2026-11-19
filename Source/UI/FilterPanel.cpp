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
        if (isMouseOver()) { g.setColour (colours::panel); g.fillRect (r); }
        const bool empty = count == 0 && ! ticked;
        auto box = Rectangle<float> (r.getX() + 4, r.getCentreY() - 5.5f, 11, 11);
        g.setColour (ticked ? colours::accent : colours::field);
        g.fillRect (box);
        g.setColour (colours::line);
        g.drawRect (box, 1.0f);
        if (ticked)
        {
            Path t; t.startNewSubPath (box.getX() + 2.4f, box.getCentreY());
            t.lineTo (box.getX() + 4.6f, box.getBottom() - 2.6f); t.lineTo (box.getRight() - 2.2f, box.getY() + 2.6f);
            g.setColour (colours::onText); g.strokePath (t, PathStrokeType (1.5f));
        }
        g.setFont (liveFont());
        g.setColour (empty ? colours::textDim : colours::text);
        auto textR = r.withTrimmedLeft (20).withTrimmedRight (4);
        auto countStr = count > 0 ? String (count) : String();
        g.drawText (option, textR.withTrimmedRight (countStr.length() * 6.5f + 2), Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.setFont (liveFont (10.0f));
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
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool empty = count == 0 && ! ticked;
        g.setColour (ticked ? colours::accent : (isMouseOver() ? colours::panelHi : colours::field));
        g.fillRect (r);
        g.setColour (colours::line);
        g.drawRect (r, 1.0f);
        g.setColour (ticked ? colours::onText : (empty ? colours::textDim : colours::text));
        g.setFont (liveFont (11.0f));
        auto label = option.upToFirstOccurrenceOf (" ", false, false);
        g.drawText (label, r.withTrimmedBottom (r.getHeight() * 0.4f), Justification::centredBottom);
        g.setFont (liveFont (9.0f));
        g.setColour (ticked ? colours::onText.withAlpha (0.7f) : colours::textDim);
        g.drawText (count > 0 ? String (count) : String ("-"), r.withTrimmedTop (r.getHeight() * 0.6f), Justification::centredTop);
    }
};

class Header : public Component
{
public:
    Header (Category c) : cat (c) {}
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::divider);
        g.fillRect (r.removeFromTop (1));
        g.setColour (colours::textDim);
        Path arrow;
        const float ax = 8, ay = r.getCentreY();
        if (collapsed) arrow.addTriangle (ax - 2.5f, ay - 3.5f, ax - 2.5f, ay + 3.5f, ax + 3, ay);
        else arrow.addTriangle (ax - 3.5f, ay - 2, ax + 3.5f, ay - 2, ax, ay + 3);
        g.fillPath (arrow);
        g.setFont (liveFont());
        g.setColour (ticks > 0 ? colours::accent : colours::textDim);
        g.drawText (categoryName (cat) + (ticks > 0 ? "  " + String (ticks) : String()),
                    r.withTrimmedLeft (18), Justification::centredLeft);
        if (ticks > 0)
        {
            g.setColour (colours::textDim);
            g.drawText ("Clear", r.withTrimmedRight (6), Justification::centredRight);
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
            h.setBounds (0, y, w, 21);
            y += 22;
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
                        t->setBounds (4 + col * cell, y + row * 30, cell, 30);
                    }
                    else t->setBounds (4, y + 62, w / 2 - 6, 19);
                }
                compatible.setVisible (true);
                compatible.setBounds (w / 2, y + 62, w / 2 - 4, 19);
                y += 62 + 24;
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
                    t->setBounds (4 + col * colW, y + row * 19, colW, 19);
                    ++i;
                }
                int rows = twoCol ? (i + 1) / 2 : i;
                y += rows * 19 + 6;
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
    countLabel.setFont (liveFont (10.5f));
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
    g.fillAll (colours::area);
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (46));
}
}
