#include "FoldersPanel.h"
#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

FoldersPanel::FoldersPanel (Library& lib) : library (lib)
{
    help.setText ("Pick the folders to sort: your Ableton projects folder, sample libraries, an external SSD... "
                  "Everything inside them is analysed, including each project's Samples folder.", dontSendNotification);
    help.setFont (liveFont());
    help.setColour (Label::textColourId, colours::textDim);
    help.setJustificationType (Justification::topLeft);
    addAndMakeVisible (help);
    list.setRowHeight (24);
    list.setColour (ListBox::backgroundColourId, colours::area);
    addAndMakeVisible (list);
    for (auto* b : { &add, &remove, &rescanAll, &ableton }) addAndMakeVisible (*b);
    addAndMakeVisible (autoRescan);
    autoRescan.setToggleState (library.getAutoRescan(), dontSendNotification);
    autoRescan.onClick = [this] { library.setAutoRescan (autoRescan.getToggleState()); };

    add.onClick = [this] {
        chooser = std::make_unique<FileChooser> ("Choose a folder of sounds",
                                                 File::getSpecialLocation (File::userMusicDirectory));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f.isDirectory()) library.addLocation (f);
                              });
    };
    remove.onClick = [this] {
        auto row = list.getSelectedRow();
        if (isPositiveAndBelow (row, locations.size())) library.removeLocation (locations[row]);
    };
    rescanAll.onClick = [this] { library.rescan (true); };
    ableton.onClick = [this] {
        auto music = File::getSpecialLocation (File::userMusicDirectory);
        for (auto f : { music.getChildFile ("Ableton/User Library"), music.getChildFile ("Ableton") })
            if (f.isDirectory()) { library.addLocation (f); return; }
    };
    library.addChangeListener (this);
    refresh();
    setSize (460, 330);
}

FoldersPanel::~FoldersPanel() { library.removeChangeListener (this); }

void FoldersPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void FoldersPanel::refresh()
{
    auto now = library.getLocations();
    if (now != locations) { locations = now; list.updateContent(); list.repaint(); }
}

void FoldersPanel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (selected) g.fillAll (colours::selection);
    File f (locations[row]);
    g.setColour (selected ? colours::onText : f.isDirectory() ? colours::text : colours::textDim);
    g.setFont (liveFont());
    g.drawText (f.getFileName() + (f.isDirectory() ? String() : String ("  (offline)")), 8, 0, w / 2, h, Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.setFont (liveFont());
    g.drawText (f.getParentDirectory().getFullPathName(), w / 2, 0, w / 2 - 8, h, Justification::centredRight, true);
}

void FoldersPanel::paint (Graphics& g)
{
    g.fillAll (colours::panel);
}

void FoldersPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    help.setBounds (r.removeFromTop (36));
    r.removeFromTop (4);
    auto bottom = r.removeFromBottom (24);
    rescanAll.setBounds (bottom.removeFromRight (150));
    autoRescan.setBounds (bottom);
    r.removeFromBottom (6);
    auto buttons = r.removeFromBottom (24);
    add.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (6);
    ableton.setBounds (buttons.removeFromLeft (170));
    remove.setBounds (buttons.removeFromRight (80));
    r.removeFromBottom (6);
    list.setBounds (r);
}
}
