#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/FilterPanel.h"
#include "UI/ResultsTable.h"
#include "UI/WaveformView.h"
#include "UI/TagEditor.h"
#include "UI/SamplerPanel.h"
#include <unordered_map>

namespace sb
{
class SampleBrowserEditor : public juce::AudioProcessorEditor,
                            public juce::DragAndDropContainer,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit SampleBrowserEditor (SampleBrowserProcessor&);
    ~SampleBrowserEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Dragging out of the plugin window hands Live a real file.
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files, bool& canMoveFiles) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh (bool rebuildModel);
    void applyFilters();
    const SampleInfo* find (const juce::String& path) const;
    void select (const SampleInfo& info, bool play);
    void togglePlay();
    void showRowMenu (const SampleInfo& info);
    void startDragOf (const SampleInfo& info);
    void syncSettingsToUi();
    void saveUiState();

    SampleBrowserProcessor& proc;
    Library& library;
    BrowserLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    FilterModel model;
    FilterModel::Result result;
    Library::Snapshot snapshot;
    juce::uint64 seenRevision = 0;
    double modelBpm = 0;
    bool modelCompat = false;
    std::unordered_map<std::string, int> pathIndex;

    // header
    juce::TextButton foldersButton { "Folders" }, rescanButton { "Rescan" }, playButton { "Play" };
    juce::Label status, tempoLabel;
    juce::TextEditor search;
    juce::ComboBox warp, sync, dragMode;
    juce::ToggleButton loopToggle { "Loop" }, autoPlay { "Auto-play" }, loadOnSelect { "To sampler" }, trim { "Skip silence" };
    juce::Label warpLabel { {}, "Warp" }, syncLabel { {}, "One-shots" };

    FilterPanel filters;
    ResultsTable table;
    std::unique_ptr<WaveformView> waveform;
    TagEditor tags;
    SamplerPanel sampler;
    juce::AudioFormatManager formats;
    juce::String lastSamplerPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowserEditor)
};
}
