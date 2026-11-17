#include "SamplerPanel.h"
#include "LookAndFeel.h"
#include "Audio/SamplerEngine.h"

namespace sb
{
using namespace juce;

class SamplerPanel::Knob : public Component
{
public:
    Knob (AudioProcessorValueTreeState& s, const String& id, const String& label)
    {
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity (180);
        addAndMakeVisible (slider);
        name.setText (label, dontSendNotification);
        value.setJustificationType (Justification::centred);
        name.setJustificationType (Justification::centred);
        name.setFont (FontOptions (11.0f));
        value.setFont (FontOptions (10.5f));
        name.setColour (Label::textColourId, colours::text);
        value.setColour (Label::textColourId, colours::textDim);
        for (auto* l : { &name, &value }) { l->setInterceptsMouseClicks (false, false); l->setMinimumHorizontalScale (0.7f); addAndMakeVisible (*l); }
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        if (auto* p = s.getParameter (id))
        {
            param = p;
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            slider.setTooltip (p->getName (64) + " (double-click to reset)");
        }
        slider.onValueChange = [this] { updateValue(); };
        updateValue();
    }
    void updateValue()
    {
        if (param != nullptr) value.setText (param->getCurrentValueAsText(), dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromTop (12));
        value.setBounds (r.removeFromBottom (12));
        slider.setBounds (r);
    }
    Slider slider;
    Label name, value;
    RangedAudioParameter* param = nullptr;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class SamplerPanel::Keyboard : public MidiKeyboardComponent
{
public:
    Keyboard (MidiKeyboardState& s) : MidiKeyboardComponent (s, horizontalKeyboard)
    {
        setColour (whiteNoteColourId, Colour (0xffe6e6e6));
        setColour (blackNoteColourId, Colour (0xff222222));
        setColour (keySeparatorLineColourId, Colour (0xff8a8a8a));
        setColour (keyDownOverlayColourId, colours::accent);
        setColour (mouseOverKeyOverlayColourId, colours::accent.withAlpha (0.3f));
        setColour (shadowColourId, Colours::transparentBlack);
        setOctaveForMiddleC (3); // Ableton naming: C3 = 60
        setKeyWidth (14.0f);
        setLowestVisibleKey (24);
    }
    int root = -1;
    void drawWhiteNote (int note, Graphics& g, Rectangle<float> area, bool down, bool over, Colour line, Colour text) override
    {
        MidiKeyboardComponent::drawWhiteNote (note, g, area, down, over, line, text);
        if (note == root) { g.setColour (colours::good); g.fillRect (area.removeFromBottom (6).reduced (2, 0)); }
    }
    void drawBlackNote (int note, Graphics& g, Rectangle<float> area, bool down, bool over, Colour fill) override
    {
        MidiKeyboardComponent::drawBlackNote (note, g, area, down, over, fill);
        if (note == root) { g.setColour (colours::good); g.fillRect (area.removeFromBottom (5).reduced (2, 0)); }
    }
};

SamplerPanel::SamplerPanel (AudioProcessorValueTreeState& s, MidiKeyboardState& keys) : apvts (s)
{
    auto knob = [&] (const char* id, const String& label) {
        auto* k = knobs.add (new Knob (s, id, label));
        addAndMakeVisible (k);
        return k;
    };
    auto toggle = [&] (const char* id, const String& label) {
        auto* t = toggles.add (new ToggleButton (label));
        addAndMakeVisible (t);
        buttonAtt.add (new AudioProcessorValueTreeState::ButtonAttachment (s, id, *t));
        return t;
    };
    auto combo = [&] (const char* id) {
        auto* c = combos.add (new ComboBox());
        if (auto* p = dynamic_cast<AudioParameterChoice*> (s.getParameter (id)))
            c->addItemList (p->choices, 1);
        addAndMakeVisible (c);
        comboAtt.add (new AudioProcessorValueTreeState::ComboBoxAttachment (s, id, *c));
        return c;
    };

    auto add = [] (Group& g, Component* c, int span = 1) { g.controls.add (c); g.spans.add (span); };

    Group sample { "SAMPLE" };
    sample.columns = 4;
    add (sample, combo (pid::mode), 2);
    add (sample, toggle (pid::autoStart, "Auto start"), 2);
    add (sample, knob (pid::start, "Start"));
    add (sample, knob (pid::end, "End"));
    auto* fadeIn = knob (pid::fadeIn, "Fade in");
    auto* fadeOut = knob (pid::fadeOut, "Fade out");
    add (sample, fadeIn); add (sample, fadeOut);
    auto* loopT = toggle (pid::loop, "Loop");
    auto* loopPos = knob (pid::loopStart, "Loop pos");
    auto* loopFade = knob (pid::loopFade, "Loop fade");
    auto* gate = toggle (pid::gate, "Gate");
    add (sample, loopT); add (sample, loopPos); add (sample, loopFade); add (sample, gate);
    for (auto* c : std::initializer_list<Component*> { loopT, loopPos, loopFade }) classicOnly.add (c);
    for (auto* c : std::initializer_list<Component*> { gate, fadeIn, fadeOut }) oneShotOnly.add (c);

    Group pitch { "PITCH" };
    pitch.columns = 2;
    add (pitch, knob (pid::transpose, "Transpose")); add (pitch, knob (pid::detune, "Detune"));
    add (pitch, knob (pid::glide, "Glide")); add (pitch, knob (pid::voices, "Voices"));

    Group amp { "AMP ENVELOPE" };
    amp.columns = 4;
    auto* a = knob (pid::attack, "Attack"); auto* d = knob (pid::decay, "Decay");
    auto* su = knob (pid::sustain, "Sustain"); auto* re = knob (pid::release, "Release");
    for (auto* c : std::initializer_list<Component*> { a, d, su, re }) add (amp, c);
    add (amp, knob (pid::velVol, "Vel > Vol")); add (amp, knob (pid::volume, "Volume"));
    add (amp, knob (pid::pan, "Pan")); add (amp, knob (pid::previewVol, "Preview"));
    for (auto* c : std::initializer_list<Component*> { a, d, su }) classicOnly.add (c);

    Group filter { "FILTER" };
    filter.columns = 4;
    add (filter, toggle (pid::filterOn, "On")); add (filter, combo (pid::filterType));
    add (filter, knob (pid::cutoff, "Freq")); add (filter, knob (pid::reso, "Res"));
    add (filter, knob (pid::filterEnv, "Env amt")); add (filter, knob (pid::filterKey, "Key trk"));
    add (filter, knob (pid::filterVel, "Vel")); add (filter, &spacer, 1);
    add (filter, knob (pid::fAttack, "F Att")); add (filter, knob (pid::fDecay, "F Dec"));
    add (filter, knob (pid::fSustain, "F Sus")); add (filter, knob (pid::fRelease, "F Rel"));

    Group lfo { "LFO" };
    lfo.columns = 4;
    lfoRateKnob = knob (pid::lfoRate, "Rate");
    lfoSyncCombo = combo (pid::lfoSyncRate);
    add (lfo, combo (pid::lfoWave), 2); add (lfo, toggle (pid::lfoSync, "Sync")); add (lfo, toggle (pid::lfoRetrig, "Retrig"));
    add (lfo, lfoRateKnob); add (lfo, lfoSyncCombo); add (lfo, knob (pid::lfoPitch, "> Pitch")); add (lfo, knob (pid::lfoFilter, "> Filter"));
    add (lfo, knob (pid::lfoVol, "> Vol")); add (lfo, knob (pid::lfoPan, "> Pan"));

    groups = { sample, pitch, amp, filter, lfo };

    keyboard = std::make_unique<Keyboard> (keys);
    addAndMakeVisible (*keyboard);
    sampleName.setFont (FontOptions (11.5f));
    sampleName.setColour (Label::textColourId, colours::good);
    sampleName.setJustificationType (Justification::centredRight);
    addAndMakeVisible (sampleName);
    startTimerHz (10);
}

SamplerPanel::~SamplerPanel() { stopTimer(); }

void SamplerPanel::setRootNote (float midi)
{
    int r = midi > 0 ? (int) std::round (midi) : -1;
    if (r != keyboard->root) { keyboard->root = r; keyboard->repaint(); }
}

void SamplerPanel::setSampleName (const String& n)
{
    sampleName.setText (n.isEmpty() ? String ("Sampler empty: select a sound") : "Sampler: " + n, dontSendNotification);
}

void SamplerPanel::timerCallback()
{
    const bool oneShot = apvts.getRawParameterValue (pid::mode)->load() > 0.5f;
    for (auto* c : classicOnly) c->setEnabled (! oneShot);
    for (auto* c : oneShotOnly) c->setEnabled (oneShot);
    const bool sync = apvts.getRawParameterValue (pid::lfoSync)->load() > 0.5f;
    lfoRateKnob->setEnabled (! sync);
    lfoSyncCombo->setEnabled (sync);
}

void SamplerPanel::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.drawHorizontalLine (0, 0, (float) getWidth());
    for (auto& gr : groups)
    {
        g.setColour (colours::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (gr.area.toFloat(), 4);
        g.setColour (colours::textDim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (gr.name, gr.area.reduced (8, 3).removeFromTop (14), Justification::topLeft);
    }
    g.setColour (colours::textDim);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("SAMPLER", getLocalBounds().reduced (8, 4).removeFromBottom (keyboard->getHeight() + 20).removeFromTop (16),
                Justification::left);
}

void SamplerPanel::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    auto kb = r.removeFromBottom (58);
    auto kbHeader = r.removeFromBottom (18);
    sampleName.setBounds (kbHeader.withTrimmedLeft (80));
    keyboard->setBounds (kb);
    // fit roughly 7 octaves; widen keys if there's room
    keyboard->setKeyWidth (jmax (10.0f, kb.getWidth() / 52.0f));
    keyboard->setLowestVisibleKey (24);

    int totalCols = 0;
    for (auto& g : groups) totalCols += g.columns;
    const int gap = 6;
    const int colW = (r.getWidth() - gap * ((int) groups.size() - 1)) / juce::jmax (1, totalCols);
    int x = r.getX();
    for (auto& g : groups)
    {
        g.area = Rectangle<int> (x, r.getY(), colW * g.columns, r.getHeight());
        auto inner = g.area.reduced (4, 4).withTrimmedTop (14);
        int rows = 1, col = 0;
        for (int i = 0; i < g.controls.size(); ++i)
        {
            int span = g.spans[i];
            if (col + span > g.columns) { ++rows; col = 0; }
            col += span;
        }
        const int rowH = juce::jmin (78, inner.getHeight() / juce::jmax (1, rows));
        col = 0; int row = 0;
        for (int i = 0; i < g.controls.size(); ++i)
        {
            auto* c = g.controls[i];
            int span = g.spans[i];
            if (col + span > g.columns) { ++row; col = 0; }
            auto cell = Rectangle<int> (inner.getX() + col * colW, inner.getY() + row * rowH, colW * span, rowH).reduced (2);
            col += span;
            if (dynamic_cast<ComboBox*> (c) != nullptr || dynamic_cast<ToggleButton*> (c) != nullptr)
                c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 22));
            else
                c->setBounds (cell);
        }
        x += g.area.getWidth() + gap;
    }
}
}
