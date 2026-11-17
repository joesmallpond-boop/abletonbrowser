#include "PluginEditor.h"
#include "UI/FoldersPanel.h"

namespace sb
{
using namespace juce;

SampleBrowserEditor::SampleBrowserEditor (SampleBrowserProcessor& p)
    : AudioProcessorEditor (p), proc (p), library (p.getLibrary()),
      sampler (p.getState(), p.getKeyboardState())
{
    setLookAndFeel (&lnf);
    formats.registerBasicFormats();
    waveform = std::make_unique<WaveformView> (formats, p.getState());

    // ---- header ----
    for (auto* c : std::initializer_list<Component*> { &foldersButton, &rescanButton, &playButton, &status, &tempoLabel, &search,
                                                       &warp, &sync, &dragMode, &loopToggle, &autoPlay, &loadOnSelect, &trim,
                                                       &warpLabel, &syncLabel })
        addAndMakeVisible (c);
    status.setFont (FontOptions (11.5f));
    status.setColour (Label::textColourId, colours::textDim);
    tempoLabel.setFont (FontOptions (12.0f, Font::bold));
    tempoLabel.setJustificationType (Justification::centredRight);
    for (auto* l : { &warpLabel, &syncLabel })
    {
        l->setFont (FontOptions (11.5f));
        l->setColour (Label::textColourId, colours::textDim);
        l->setJustificationType (Justification::centredRight);
    }
    search.setTextToShowWhenEmpty ("Search names, folders, tags...", colours::textDim);
    search.onTextChange = [this] { proc.filterState.search = search.getText(); applyFilters(); };
    search.onEscapeKey = [this] { search.clear(); proc.filterState.search.clear(); applyFilters(); table.getTable().grabKeyboardFocus(); };
    search.onReturnKey = [this] { table.getTable().grabKeyboardFocus(); };

    foldersButton.setTooltip ("Choose which folders are scanned");
    foldersButton.onClick = [this] {
        auto panel = std::make_unique<FoldersPanel> (library);
        CallOutBox::launchAsynchronously (std::move (panel), foldersButton.getScreenBounds(), nullptr);
    };
    rescanButton.setTooltip ("Look for new or changed sounds now");
    rescanButton.onClick = [this] { library.rescan(); };
    playButton.setTooltip ("Preview on/off. Space bar starts and stops Live: the preview plays in time with it");
    playButton.onClick = [this] { togglePlay(); };

    warp.addItem ("Off", 1); warp.addItem ("Re-Pitch", 2); warp.addItem ("Stretch", 3);
    warp.setTooltip ("Warp previews to the project tempo. Stretch keeps the pitch; Re-Pitch speeds up/slows down like a record");
    warp.onChange = [this] { proc.previewSettings.warp = (WarpMode) (warp.getSelectedId() - 1); proc.previewSettingsChanged(); };
    sync.addItem ("Immediately", 1); sync.addItem ("Next beat", 2); sync.addItem ("Next bar", 3);
    sync.setTooltip ("Previews follow Live's transport. Loops with a tempo are locked to the song position (they start on bar 1 "
                     "of the loop and stay in time). One-shots start on the next beat or bar; tick Loop to repeat them on it.");
    sync.onChange = [this] { proc.previewSettings.sync = (SyncMode) (sync.getSelectedId() - 1); };
    dragMode.addItem ("Drag original file", 1);
    dragMode.addItem ("Drag trimmed + tempo-matched", 2);
    dragMode.setTooltip ("What you get when you drag a sound into Live");
    dragMode.onChange = [this] { proc.previewSettings.dragProcessed = dragMode.getSelectedId() == 2; };
    loopToggle.setTooltip ("Repeat one-shots on every beat/bar while Live plays (loops always loop)");
    loopToggle.onClick = [this] { proc.previewSettings.loop = loopToggle.getToggleState(); proc.previewSettingsChanged(); };
    autoPlay.setTooltip ("Play sounds as soon as they're selected");
    autoPlay.onClick = [this] { proc.previewSettings.autoPlay = autoPlay.getToggleState(); };
    loadOnSelect.setTooltip ("Load the selected sound into the sampler so you can play it straight away");
    loadOnSelect.onClick = [this] { proc.previewSettings.loadOnSelect = loadOnSelect.getToggleState(); };
    trim.setTooltip ("Previews start where the sound begins, skipping leading silence");
    trim.onClick = [this] { proc.previewSettings.trimSilence = trim.getToggleState(); proc.previewSettingsChanged(); };

    // ---- filters ----
    addAndMakeVisible (filters);
    filters.onToggle = [this] (Category c, String o) { proc.filterState.toggle (c, o); applyFilters(); };
    filters.onClearCategory = [this] (Category c) { proc.filterState.ticked[(size_t) c].clear(); applyFilters(); };
    filters.onClearAll = [this] {
        for (auto& s : proc.filterState.ticked) s.clear();
        proc.filterState.favouritesOnly = false;
        proc.filterState.search.clear();
        search.clear();
        applyFilters();
    };
    filters.onFavouritesToggled = [this] { proc.filterState.favouritesOnly = ! proc.filterState.favouritesOnly; applyFilters(); };
    filters.onCompatibleToggled = [this] { proc.filterState.compatibleKeys = ! proc.filterState.compatibleKeys; refresh (true); };

    // ---- table ----
    addAndMakeVisible (table);
    table.onSelect = [this] (const SampleInfo& s) { select (s, proc.previewSettings.autoPlay); };
    table.onDoubleClick = [this] (const SampleInfo& s) { proc.loadIntoSampler (s); proc.audition (s); };
    table.onToggleFavourite = [this] (const SampleInfo& s) { library.editSample (s.path, [] (SampleInfo& x) { x.favourite = ! x.favourite; }); };
    table.onRightClick = [this] (const SampleInfo& s, const MouseEvent&) { showRowMenu (s); };
    if (proc.uiState.isObject()) table.setSortState (proc.uiState["sort"]);

    // ---- waveform, tags, sampler ----
    addAndMakeVisible (*waveform);
    waveform->onClickPlay = [this] { if (auto* s = find (proc.selectedPath)) proc.audition (*s); };
    waveform->onStartDrag = [this] { if (auto* s = find (proc.selectedPath)) startDragOf (*s); };
    addAndMakeVisible (tags);
    tags.onEdit = [this] (const String& path, TagEditor::Edit e) {
        library.editSample (path, e);
    };
    tags.onReanalyse = [this] (const String& path) { library.reanalyse (path); };
    tags.onLoadSampler = [this] (const String& path) { if (auto* s = find (path)) proc.loadIntoSampler (*s); };
    tags.onReveal = [] (const String& path) { File (path).revealToUser(); };
    addAndMakeVisible (sampler);

    library.addChangeListener (this);
    proc.addChangeListener (this);
    syncSettingsToUi();

    setResizable (true, true);
    setResizeLimits (1050, 700, 2600, 1600);
    int w = 1320, h = 860;
    if (proc.uiState.isObject())
    {
        w = (int) proc.uiState.getProperty ("w", w);
        h = (int) proc.uiState.getProperty ("h", h);
    }
    setSize (w, h);
    refresh (true);
    startTimerHz (20);

    if (library.getLocations().isEmpty())
        MessageManager::callAsync ([safe = Component::SafePointer<SampleBrowserEditor> (this)] {
            if (safe != nullptr) safe->foldersButton.triggerClick();
        });
}

SampleBrowserEditor::~SampleBrowserEditor()
{
    saveUiState();
    library.removeChangeListener (this);
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SampleBrowserEditor::saveUiState()
{
    auto* o = new DynamicObject();
    o->setProperty ("w", getWidth());
    o->setProperty ("h", getHeight());
    o->setProperty ("sort", table.getSortState());
    proc.uiState = var (o);
}

void SampleBrowserEditor::syncSettingsToUi()
{
    auto& s = proc.previewSettings;
    warp.setSelectedId ((int) s.warp + 1, dontSendNotification);
    sync.setSelectedId (s.sync == SyncMode::InPhase ? 3 : (int) s.sync + 1, dontSendNotification);
    dragMode.setSelectedId (s.dragProcessed ? 2 : 1, dontSendNotification);
    loopToggle.setToggleState (s.loop, dontSendNotification);
    autoPlay.setToggleState (s.autoPlay, dontSendNotification);
    loadOnSelect.setToggleState (s.loadOnSelect, dontSendNotification);
    trim.setToggleState (s.trimSilence, dontSendNotification);
    search.setText (proc.filterState.search, false);
}

// ------------------------------------------------------------------ data
void SampleBrowserEditor::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &proc) { syncSettingsToUi(); refresh (true); return; }
    refresh (library.revision() != seenRevision);
}

void SampleBrowserEditor::refresh (bool rebuildModel)
{
    if (rebuildModel || snapshot == nullptr)
    {
        snapshot = library.snapshot();
        seenRevision = library.revision();
        modelBpm = proc.getHostBpm();
        modelCompat = proc.filterState.compatibleKeys;
        model.rebuild (*snapshot, modelBpm, modelCompat);
        pathIndex.clear();
        for (size_t i = 0; i < snapshot->size(); ++i) pathIndex[(*snapshot)[i].path.toStdString()] = (int) i;
    }
    applyFilters();
    // keep the side panels in step with edits
    if (auto* s = find (proc.selectedPath))
    {
        tags.setSample (s);
        waveform->setSample (s);
    }
}

void SampleBrowserEditor::applyFilters()
{
    if (snapshot == nullptr) return;
    result = model.apply (*snapshot, proc.filterState);
    filters.update (model, result, proc.filterState, (int) snapshot->size());
    table.setData (snapshot, result.indices);
    table.selectPath (proc.selectedPath, false);
}

const SampleInfo* SampleBrowserEditor::find (const String& path) const
{
    if (snapshot == nullptr) return nullptr;
    auto it = pathIndex.find (path.toStdString());
    return it != pathIndex.end() ? &(*snapshot)[(size_t) it->second] : nullptr;
}

// ------------------------------------------------------------------ actions
void SampleBrowserEditor::select (const SampleInfo& info, bool play)
{
    proc.selectedPath = info.path;
    tags.setSample (&info);
    waveform->setSample (&info);
    if (proc.previewSettings.loadOnSelect) proc.loadIntoSampler (info);
    if (play) proc.audition (info);
}

void SampleBrowserEditor::togglePlay()
{
    if (proc.getPreview().isArmed()) { proc.stopAudition(); return; }
    if (auto* s = find (proc.selectedPath)) proc.audition (*s);
}

void SampleBrowserEditor::startDragOf (const SampleInfo& info)
{
    startDragging ("sample:" + info.path, waveform.get(), ScaledImage(), true);
}

bool SampleBrowserEditor::shouldDropFilesWhenDraggedExternally (const DragAndDropTarget::SourceDetails& details,
                                                               StringArray& files, bool& canMoveFiles)
{
    auto desc = details.description.toString();
    if (! desc.startsWith ("sample:")) return false;
    auto path = desc.fromFirstOccurrenceOf ("sample:", false, false);
    const SampleInfo* info = find (path);
    if (info == nullptr) return false;
    auto f = proc.fileForDrag (*info, proc.previewSettings.dragProcessed);
    if (! f.existsAsFile()) return false;
    files.add (f.getFullPathName());
    canMoveFiles = false;
    return true;
}

void SampleBrowserEditor::showRowMenu (const SampleInfo& info)
{
    PopupMenu m;
    m.addItem ("Play", [this, info] { proc.audition (info); });
    m.addItem ("Load into sampler", [this, info] { proc.loadIntoSampler (info); });
    m.addItem (info.favourite ? "Remove from favourites" : "Add to favourites",
               [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.favourite = ! s.favourite; }); });
    m.addSeparator();
    PopupMenu inst;
    for (int i = 1; i < (int) Instrument::Count; ++i)
        inst.addItem (instrumentName ((Instrument) i), true, info.instrument() == (Instrument) i,
                      [this, p = info.path, i] { library.editSample (p, [i] (SampleInfo& s) { s.userInstrument = i; }); });
    m.addSubMenu ("Set instrument", inst);
    PopupMenu key;
    for (int mode = 0; mode < 2; ++mode)
    {
        PopupMenu sub;
        for (int r = 0; r < 12; ++r)
            sub.addItem (theory::keyName (r, mode), true, info.keyRoot() == r && info.keyMode() == mode,
                         [this, p = info.path, r, mode] { library.editSample (p, [r, mode] (SampleInfo& s) { s.userKeyRoot = r; s.userKeyMode = mode; }); });
        key.addSubMenu (mode == 0 ? "Major" : "Minor", sub);
    }
    key.addItem ("No key", [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.userKeyRoot = -1; }); });
    m.addSubMenu ("Set key", key);
    if (info.bpm() > 0)
    {
        m.addItem ("Tempo: halve (" + String (info.bpm() / 2, 1) + ")", [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.userBpm = s.bpm() / 2; }); });
        m.addItem ("Tempo: double (" + String (info.bpm() * 2, 1) + ")", [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.userBpm = s.bpm() * 2; }); });
    }
    if (proc.getHostBpm() > 0)
        m.addItem ("Tempo: use project tempo (" + String (proc.getHostBpm(), 1) + ")",
                   [this, p = info.path, b = proc.getHostBpm()] { library.editSample (p, [b] (SampleInfo& s) { s.userBpm = b; }); });
    m.addItem (info.type() == SoundType::Loop ? "Mark as one-shot" : "Mark as loop",
               [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.userType = s.type() == SoundType::Loop ? 0 : 1; }); });
    m.addSeparator();
    m.addItem ("Reset tags to detected", [this, p = info.path] { library.editSample (p, [] (SampleInfo& s) { s.clearUserEdits(); }); });
    m.addItem ("Re-analyse", [this, p = info.path] { library.reanalyse (p); });
    m.addItem ("Show file", [p = info.path] { File (p).revealToUser(); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

bool SampleBrowserEditor::keyPressed (const KeyPress& k)
{
    // Space is left for Live, so it starts/stops the set (and the preview follows the transport).
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'F') { search.grabKeyboardFocus(); return true; }
    return false;
}

// ------------------------------------------------------------------ timer
void SampleBrowserEditor::timerCallback()
{
    // Dev aid: BASH_BROWSER_SNAPSHOT=/path.png saves a screenshot of the window after a few seconds.
    static int ticks = 0;
    if (++ticks == 40)
        if (auto* name = std::getenv ("BASH_BROWSER_SNAPSHOT_SELECT"))
            if (snapshot != nullptr)
                for (auto& s : *snapshot)
                    if (s.path.contains (name)) { table.selectPath (s.path, true); select (s, false); break; }
    if (ticks == 120)
        if (auto* path = std::getenv ("BASH_BROWSER_SNAPSHOT"))
        {
            auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
            File f (path); f.deleteFile();
            FileOutputStream fo (f);
            PNGImageFormat().writeImageToStream (img, fo);
        }

    auto& preview = proc.getPreview();
    const auto playingPath = preview.isArmed() ? preview.currentPath() : String();
    table.setPlayingPath (playingPath);
    playButton.setButtonText (playingPath.isNotEmpty() ? "Stop" : "Play");
    playButton.setToggleState (playingPath.isNotEmpty(), dontSendNotification);

    Array<double> voices;
    const auto smpPath = proc.samplerPath();
    if (smpPath == proc.selectedPath) proc.getSampler().getVoicePositions (voices);
    waveform->setInSampler (smpPath.isNotEmpty() && smpPath == proc.selectedPath);
    waveform->setPlayheads (playingPath == proc.selectedPath ? preview.playheadSourceSec() : -1.0, voices,
                            playingPath == proc.selectedPath && preview.isWaiting());
    table.setSamplerPath (smpPath);
    if (smpPath != lastSamplerPath)
    {
        lastSamplerPath = smpPath;
        sampler.setSampleName (File (smpPath).getFileNameWithoutExtension());
    }
    if (auto s = proc.getSampler().getSample()) sampler.setRootNote (s->rootMidi);
    else sampler.setRootNote (-1);

    // keep the sampler mapping in step with edits to the root note
    if (auto* info = find (smpPath))
        if (auto s = proc.getSampler().getSample())
            if (std::abs (s->rootMidi - (info->rootMidi() > 0 ? info->rootMidi() : 60.0f)) > 0.001f)
                proc.reloadSamplerMapping (*info);

    int done = 0, total = 0; String st;
    library.getProgress (done, total, st);
    if (library.isScanning() && total > 0) st = "Analysing " + String (done) + " / " + String (total);
    if (library.getLocations().isEmpty()) st = "No folders yet: click Folders to add your sounds";
    status.setText (st, dontSendNotification);

    const double bpm = proc.getHostBpm();
    tempoLabel.setText (String (bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM" + (proc.isHostPlaying() ? "  >" : ""),
                        dontSendNotification);
    tempoLabel.setColour (Label::textColourId, proc.isHostPlaying() ? colours::good : colours::textDim);

    // "Fits project tempo" depends on the live tempo
    if (std::abs (bpm - modelBpm) > 0.01 && ! proc.filterState.ticked[(size_t) Category::Tempo].empty())
        refresh (true);
}

// ------------------------------------------------------------------ layout
void SampleBrowserEditor::paint (Graphics& g)
{
    g.fillAll (colours::bg);
    auto header = getLocalBounds().removeFromTop (44);
    g.setColour (colours::panel);
    g.fillRect (header);
    g.setColour (colours::line);
    g.drawHorizontalLine (header.getBottom() - 1, 0, (float) getWidth());
    g.setColour (colours::accent);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText ("BASH BROWSER", 12, 0, 150, 28, Justification::bottomLeft);
    g.setColour (colours::textDim);
    g.setFont (FontOptions (10.0f));
    g.drawText ("by Kalabash", 12, 28, 150, 12, Justification::topLeft);
}

void SampleBrowserEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (44).reduced (8, 9);
    header.removeFromLeft (150);
    foldersButton.setBounds (header.removeFromLeft (70)); header.removeFromLeft (4);
    rescanButton.setBounds (header.removeFromLeft (64)); header.removeFromLeft (8);
    tempoLabel.setBounds (header.removeFromRight (96));
    header.removeFromRight (6);
    dragMode.setBounds (header.removeFromRight (190)); header.removeFromRight (8);
    trim.setBounds (header.removeFromRight (96));
    loadOnSelect.setBounds (header.removeFromRight (88));
    autoPlay.setBounds (header.removeFromRight (82));
    loopToggle.setBounds (header.removeFromRight (56)); header.removeFromRight (4);
    sync.setBounds (header.removeFromRight (104));
    syncLabel.setBounds (header.removeFromRight (62));
    warp.setBounds (header.removeFromRight (92));
    warpLabel.setBounds (header.removeFromRight (40)); header.removeFromRight (4);
    playButton.setBounds (header.removeFromRight (60)); header.removeFromRight (8);

    auto samplerArea = r.removeFromBottom (jlimit (290, 360, (int) (getHeight() * 0.37)));
    sampler.setBounds (samplerArea);
    filters.setBounds (r.removeFromLeft (jlimit (250, 330, getWidth() / 5)));
    tags.setBounds (r.removeFromRight (jlimit (270, 340, getWidth() / 5)));
    waveform->setBounds (r.removeFromBottom (jlimit (110, 180, r.getHeight() / 3)));
    auto bar = r.removeFromTop (32).reduced (6, 4);
    status.setBounds (bar.removeFromRight (jmin (260, bar.getWidth() / 3)));
    bar.removeFromRight (6);
    search.setBounds (bar);
    table.setBounds (r);
}
}
