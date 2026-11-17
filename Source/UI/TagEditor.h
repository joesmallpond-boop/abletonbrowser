#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library/SampleInfo.h"

namespace sb
{
// Right-hand panel: shows the selected sound's tags and lets you correct them.
class TagEditor : public juce::Component
{
public:
    TagEditor();
    void setSample (const SampleInfo* info);

    using Edit = std::function<void (SampleInfo&)>;
    std::function<void (const juce::String& path, Edit)> onEdit;
    std::function<void (const juce::String& path)> onReanalyse, onLoadSampler, onReveal;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void edit (Edit e) { if (current && onEdit) onEdit (current->path, std::move (e)); }
    void commitBpm();
    static juce::StringArray splitList (const juce::String&);

    std::unique_ptr<SampleInfo> current;
    juce::Label title, pathLabel, details, character;
    juce::ToggleButton favourite { "Favourite" };
    juce::Label instLabel { {}, "Instrument" }, keyLabel { {}, "Key" }, bpmLabel { {}, "Tempo" }, typeLabel { {}, "Type" },
                rootLabel { {}, "Sampler root" }, styleLabel { {}, "Style" }, tagsLabel { {}, "My tags" };
    juce::ComboBox instrument, keyRoot, keyMode, type;
    juce::TextEditor bpm, styles, tags;
    juce::TextButton half { "/2" }, twice { "x2" }, bpmAuto { "Auto" }, rootAuto { "Auto" };
    juce::Slider root;
    juce::TextButton resetAll { "Reset to detected" }, reanalyse { "Re-analyse" }, reveal { "Show file" }, loadSampler { "Load into sampler" };
};
}
