#include "TagEditor.h"
#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

TagEditor::TagEditor()
{
    title.setFont (FontOptions (15.0f, Font::bold));
    title.setColour (Label::textColourId, colours::text);
    pathLabel.setFont (FontOptions (11.0f));
    pathLabel.setColour (Label::textColourId, colours::textDim);
    pathLabel.setMinimumHorizontalScale (0.6f);
    details.setFont (FontOptions (11.5f));
    details.setColour (Label::textColourId, colours::textDim);
    details.setJustificationType (Justification::topLeft);
    character.setFont (FontOptions (11.5f));
    character.setColour (Label::textColourId, colours::text);
    character.setJustificationType (Justification::topLeft);
    for (auto* l : { &title, &pathLabel, &details, &character }) addAndMakeVisible (*l);
    for (auto* l : { &instLabel, &keyLabel, &bpmLabel, &typeLabel, &rootLabel, &styleLabel, &tagsLabel })
    {
        l->setFont (FontOptions (11.5f));
        l->setColour (Label::textColourId, colours::textDim);
        addAndMakeVisible (*l);
    }

    addAndMakeVisible (favourite);
    favourite.onClick = [this] { bool v = favourite.getToggleState(); edit ([v] (SampleInfo& s) { s.favourite = v; }); };

    addAndMakeVisible (instrument);
    instrument.onChange = [this] {
        int id = instrument.getSelectedId();
        edit ([id] (SampleInfo& s) { s.userInstrument = id == 1 ? -1 : id - 100; });
    };

    addAndMakeVisible (keyRoot);
    addAndMakeVisible (keyMode);
    keyMode.addItem ("Major", 1); keyMode.addItem ("Minor", 2); keyMode.addItem ("Single note", 3);
    auto keyChanged = [this] {
        int id = keyRoot.getSelectedId();
        int mode = keyMode.getSelectedId() - 1;
        if (mode < 0) mode = Major;
        edit ([id, mode] (SampleInfo& s) {
            if (id == 1) { s.userKeyRoot = -2; return; }
            if (id == 2) { s.userKeyRoot = -1; return; }
            s.userKeyRoot = id - 10; s.userKeyMode = mode;
        });
    };
    keyRoot.onChange = keyChanged;
    keyMode.onChange = keyChanged;

    addAndMakeVisible (bpm);
    bpm.setInputRestrictions (7, "0123456789.");
    bpm.setJustification (Justification::centredLeft);
    bpm.onReturnKey = [this] { commitBpm(); };
    bpm.onFocusLost = [this] { commitBpm(); };
    for (auto* b : { &half, &twice, &bpmAuto, &rootAuto }) addAndMakeVisible (*b);
    half.onClick = [this] { edit ([] (SampleInfo& s) { if (s.bpm() > 0) s.userBpm = s.bpm() / 2; }); };
    twice.onClick = [this] { edit ([] (SampleInfo& s) { if (s.bpm() > 0) s.userBpm = s.bpm() * 2; }); };
    bpmAuto.onClick = [this] { edit ([] (SampleInfo& s) { s.userBpm = -1; }); };

    addAndMakeVisible (type);
    type.addItem ("Auto", 1); type.addItem ("One-shot", 2); type.addItem ("Loop", 3);
    type.onChange = [this] { int id = type.getSelectedId(); edit ([id] (SampleInfo& s) { s.userType = id == 1 ? -1 : id - 2; }); };

    addAndMakeVisible (root);
    root.setSliderStyle (Slider::LinearBar);
    root.setRange (0.0, 127.0, 0.01);
    root.setColour (Slider::trackColourId, colours::panelHi);
    root.setColour (Slider::backgroundColourId, colours::bg);
    root.textFromValueFunction = [] (double v) {
        int n = (int) std::round (v);
        int ct = (int) std::round ((v - n) * 100);
        return theory::midiNoteName ((float) n) + (ct != 0 ? (ct > 0 ? " +" : " ") + String (ct) + " ct" : String());
    };
    root.updateText();
    root.setTooltip ("The note that plays the sample at its original pitch. Drag to change, or type e.g. 43.5");
    root.onDragEnd = [this] { double v = root.getValue(); edit ([v] (SampleInfo& s) { s.userRootMidi = (float) v; }); };
    root.onValueChange = [this] { if (! root.isMouseButtonDown()) { double v = root.getValue(); edit ([v] (SampleInfo& s) { s.userRootMidi = (float) v; }); } };
    rootAuto.onClick = [this] { edit ([] (SampleInfo& s) { s.userRootMidi = -1000; }); };

    for (auto* t : { &styles, &tags })
    {
        addAndMakeVisible (*t);
        t->setTextToShowWhenEmpty ("comma separated", colours::textDim);
    }
    styles.onReturnKey = styles.onFocusLost = [this] {
        auto list = splitList (styles.getText());
        edit ([list] (SampleInfo& s) {
            juce::StringArray extra;
            for (auto& v : list) if (! s.autoStyles.contains (v)) extra.add (v);
            s.userStyles = extra;
        });
    };
    tags.onReturnKey = tags.onFocusLost = [this] {
        auto list = splitList (tags.getText());
        edit ([list] (SampleInfo& s) { s.userTags = list; });
    };

    for (auto* b : { &resetAll, &reanalyse, &reveal, &loadSampler }) addAndMakeVisible (*b);
    resetAll.onClick = [this] { edit ([] (SampleInfo& s) { s.clearUserEdits(); }); };
    reanalyse.onClick = [this] { if (current && onReanalyse) onReanalyse (current->path); };
    reveal.onClick = [this] { if (current && onReveal) onReveal (current->path); };
    loadSampler.onClick = [this] { if (current && onLoadSampler) onLoadSampler (current->path); };

    setSample (nullptr);
}

StringArray TagEditor::splitList (const String& text)
{
    StringArray a;
    a.addTokens (text, ",", "\"");
    a.trim();
    a.removeEmptyStrings();
    a.removeDuplicates (true);
    return a;
}

void TagEditor::commitBpm()
{
    if (! current) return;
    auto t = bpm.getText().trim();
    double v = t.isEmpty() ? 0.0 : t.getDoubleValue();
    if (std::abs (v - current->bpm()) < 0.001) return;
    edit ([v] (SampleInfo& s) { s.userBpm = juce::jlimit (0.0, 999.0, v); });
}

void TagEditor::setSample (const SampleInfo* s)
{
    const bool has = s != nullptr;
    for (auto* c : getChildren()) c->setEnabled (has);
    if (! has)
    {
        current.reset();
        title.setText ("No sound selected", dontSendNotification);
        pathLabel.setText ({}, dontSendNotification);
        details.setText ({}, dontSendNotification);
        character.setText ({}, dontSendNotification);
        return;
    }
    current = std::make_unique<SampleInfo> (*s);
    title.setText (s->fileName(), dontSendNotification);
    pathLabel.setText (s->path, dontSendNotification);
    pathLabel.setTooltip (s->path);
    favourite.setToggleState (s->favourite, dontSendNotification);

    instrument.clear (dontSendNotification);
    instrument.addItem ("Auto (" + instrumentName (s->autoInstrument) + ")", 1);
    for (int i = 1; i < (int) Instrument::Count; ++i) instrument.addItem (instrumentName ((Instrument) i), 100 + i);
    instrument.addItem (instrumentName (Instrument::Unknown), 100);
    instrument.setSelectedId (s->userInstrument >= 0 ? 100 + s->userInstrument : 1, dontSendNotification);

    keyRoot.clear (dontSendNotification);
    keyRoot.addItem ("Auto (" + theory::keyName (s->autoKeyRoot, s->autoKeyMode) + ")", 1);
    keyRoot.addItem ("No key", 2);
    for (int pc = 0; pc < 12; ++pc) keyRoot.addItem (theory::pitchClassName (pc), 10 + pc);
    if (s->userKeyRoot == -2) keyRoot.setSelectedId (1, dontSendNotification);
    else if (s->userKeyRoot == -1) keyRoot.setSelectedId (2, dontSendNotification);
    else keyRoot.setSelectedId (10 + s->userKeyRoot, dontSendNotification);
    keyMode.setSelectedId (s->keyMode() + 1, dontSendNotification);
    keyMode.setEnabled (s->userKeyRoot >= 0);

    bpm.setText (s->bpm() > 0 ? String (s->bpm(), 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : String(), false);
    type.setSelectedId (s->userType < 0 ? 1 : s->userType + 2, dontSendNotification);
    root.setValue (s->rootMidi() > 0 ? s->rootMidi() : 60.0, dontSendNotification);
    styles.setText (s->styles().joinIntoString (", "), false);
    tags.setText (s->userTags.joinIntoString (", "), false);

    StringArray ch;
    for (int b = 0; b < chNumFlags; ++b) if (s->character & (1u << b)) ch.add (characterName (b));
    character.setText (ch.isEmpty() ? String ("-") : ch.joinIntoString (", "), dontSendNotification);

    String d;
    d << tonalityName (s->tonality);
    if (s->medianPitchMidi > 0 && s->tonality != Tonality::Atonal) d << ", around " << theory::midiNoteName (s->medianPitchMidi);
    d << "\nDetected: " << instrumentName (s->autoInstrument) << (s->instrumentFromName ? " (from name)" : "")
      << ", " << theory::keyName (s->autoKeyRoot, s->autoKeyMode)
      << (s->autoBpm > 0 ? ", " + String (s->autoBpm, 1) + " bpm" : String());
    if (s->projectBpm > 0) d << "\nProject tempo " << String (s->projectBpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " bpm";
    d << "\n" << String (s->effectiveLength(), 2) << " s, " << String (s->sampleRate / 1000.0, 1) << " kHz, "
      << (s->numChannels > 1 ? "stereo" : "mono") << ", peak " << String (s->peakDb, 1) << " dB";
    details.setText (d, dontSendNotification);
    resized();
}

void TagEditor::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.drawVerticalLine (0, 0, (float) getHeight());
}

void TagEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    title.setBounds (r.removeFromTop (22));
    pathLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    favourite.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    auto row = [&] (Label& l) { auto line = r.removeFromTop (24); l.setBounds (line.removeFromLeft (84)); r.removeFromTop (4); return line; };
    instrument.setBounds (row (instLabel));
    {
        auto line = row (keyLabel);
        keyRoot.setBounds (line.removeFromLeft (line.getWidth() / 2 - 2));
        line.removeFromLeft (4);
        keyMode.setBounds (line);
    }
    {
        auto line = row (bpmLabel);
        bpmAuto.setBounds (line.removeFromRight (40));
        line.removeFromRight (3);
        twice.setBounds (line.removeFromRight (28));
        line.removeFromRight (3);
        half.setBounds (line.removeFromRight (28));
        line.removeFromRight (4);
        bpm.setBounds (line);
    }
    type.setBounds (row (typeLabel));
    {
        auto line = row (rootLabel);
        rootAuto.setBounds (line.removeFromRight (40));
        line.removeFromRight (4);
        root.setBounds (line);
    }
    styles.setBounds (row (styleLabel));
    tags.setBounds (row (tagsLabel));
    r.removeFromTop (2);
    character.setBounds (r.removeFromTop (32));
    details.setBounds (r.removeFromTop (70));
    r.removeFromTop (4);
    auto b1 = r.removeFromTop (24);
    loadSampler.setBounds (b1.removeFromLeft (b1.getWidth() / 2 - 2));
    b1.removeFromLeft (4);
    reveal.setBounds (b1);
    r.removeFromTop (4);
    auto b2 = r.removeFromTop (24);
    resetAll.setBounds (b2.removeFromLeft (b2.getWidth() / 2 - 2));
    b2.removeFromLeft (4);
    reanalyse.setBounds (b2);
}
}
