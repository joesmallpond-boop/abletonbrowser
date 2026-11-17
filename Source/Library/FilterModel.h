#pragma once
#include "SampleInfo.h"
#include <set>

namespace sb
{
enum class Category : int
{
    Instrument = 0, Key, Tempo, Type, Tonality, Register, Character, Length, Style, Tags, Source, Count
};
juce::String categoryName (Category c);

// Which ticks are on. Within a category ticks are OR'ed; across categories they're AND'ed.
struct FilterState
{
    std::array<std::set<juce::String>, (size_t) Category::Count> ticked;  // option labels
    bool favouritesOnly = false;
    bool compatibleKeys = false;   // include relative keys and single notes that fit the scale
    juce::String search;

    bool isTicked (Category c, const juce::String& o) const { return ticked[(size_t) c].count (o) > 0; }
    void toggle (Category c, const juce::String& o)
    {
        auto& s = ticked[(size_t) c];
        if (s.count (o)) s.erase (o); else s.insert (o);
    }
    bool anyTicked() const
    {
        for (auto& s : ticked) if (! s.empty()) return true;
        return favouritesOnly || search.isNotEmpty();
    }
    juce::var toVar() const;
    void fromVar (const juce::var& v);
};

// Turns the library into tick-box options with counts, and filters it.
class FilterModel
{
public:
    // Rebuild option lists and each sample's memberships. hostBpm is used by "Fits project tempo".
    void rebuild (const std::vector<SampleInfo>& samples, double hostBpm, bool compatibleKeys);

    // Options shown for a category (fixed lists, or ones collected from the library).
    const juce::StringArray& options (Category c) const { return opts[(size_t) c]; }

    struct Result
    {
        std::vector<int> indices;                                     // matching samples
        std::array<std::vector<int>, (size_t) Category::Count> counts; // per option, given the other categories' ticks
    };
    Result apply (const std::vector<SampleInfo>& samples, const FilterState& state) const;

    static const juce::StringArray& keyOptions();   // 24 keys ("C maj" ... "B min") + "No key"
    static juce::StringArray memberships (const SampleInfo& s, Category c, double hostBpm, bool compatibleKeys);

private:
    std::array<juce::StringArray, (size_t) Category::Count> opts;
    // memberships[sample][category] = option indices
    std::vector<std::array<std::vector<int>, (size_t) Category::Count>> member;
    std::vector<juce::String> searchText;
};
}
