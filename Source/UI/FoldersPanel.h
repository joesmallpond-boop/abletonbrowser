#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library/Library.h"

namespace sb
{
// Pop-up for choosing which folders the library scans.
class FoldersPanel : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit FoldersPanel (Library& lib);
    ~FoldersPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return locations.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    Library& library;
    juce::StringArray locations;
    juce::ListBox list { {}, this };
    juce::TextButton add { "Add folder..." }, remove { "Remove" }, rescanAll { "Re-analyse everything" }, ableton { "Add Ableton User Library" };
    juce::ToggleButton autoRescan { "Watch for new sounds" };
    juce::Label help;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
