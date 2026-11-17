#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library/FilterModel.h"

namespace sb
{
// Left-hand panel: every category as a group of tick boxes with live counts.
class FilterPanel : public juce::Component
{
public:
    FilterPanel();
    ~FilterPanel() override;

    void update (const FilterModel& model, const FilterModel::Result& result, const FilterState& state, int totalSamples);
    std::function<void (Category, juce::String)> onToggle;   // tick clicked
    std::function<void (Category)> onClearCategory;
    std::function<void()> onClearAll, onFavouritesToggled, onCompatibleToggled;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    juce::TextButton clearAll { "Clear all" };
    juce::ToggleButton favourites { "Favourites only" };
    juce::Label countLabel;
};
}
