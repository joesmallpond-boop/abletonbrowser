#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace sb
{
// Bottom panel: Simpler-style controls and a playable keyboard.
class SamplerPanel : public juce::Component, private juce::Timer
{
public:
    SamplerPanel (juce::AudioProcessorValueTreeState& state, juce::MidiKeyboardState& keys);
    ~SamplerPanel() override;

    void setRootNote (float midi);          // highlighted on the keyboard
    void setSampleName (const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Group
    {
        juce::String name;
        juce::Array<juce::Component*> controls;
        juce::Array<int> spans;
        int columns = 4;
        juce::Rectangle<int> area;
    };

    class Knob;
    class Keyboard;
    juce::AudioProcessorValueTreeState& apvts;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    std::vector<Group> groups;
    std::unique_ptr<Keyboard> keyboard;
    juce::Label sampleName;
    juce::Component spacer;
    Knob* lfoRateKnob = nullptr;
    juce::ComboBox* lfoSyncCombo = nullptr;
    juce::Array<juce::Component*> oneShotOnly, classicOnly;
};
}
