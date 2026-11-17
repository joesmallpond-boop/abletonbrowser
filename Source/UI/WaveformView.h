#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Library/SampleInfo.h"

namespace sb
{
// Waveform of the selected sound. When that sound is in the sampler it shows (and lets you drag)
// the start, end and loop markers, like Simpler. Drag from the waveform to drop the sound into Live.
class WaveformView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    WaveformView (juce::AudioFormatManager& formats, juce::AudioProcessorValueTreeState& state);
    ~WaveformView() override;

    void setSample (const SampleInfo* info);
    void setInSampler (bool b) { if (b != inSampler) { inSampler = b; repaint(); } }
    void setPlayheads (double previewSec, const juce::Array<double>& voiceSecs, bool previewWaiting);

    std::function<void()> onClickPlay;
    std::function<void()> onStartDrag;   // drag out to Live

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Marker { None, Start, End, LoopStart };
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;
    float xForSec (double s) const;
    double secForX (float x) const;
    Marker markerAt (float x) const;
    double paramSec (const char* id) const;
    void setParamFromSec (const char* id, double sec);
    double regionStartSec() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::AudioThumbnailCache cache { 32 };
    juce::AudioThumbnail thumb;
    std::unique_ptr<SampleInfo> info;
    bool inSampler = false;
    double previewPos = -1;
    bool waiting = false;
    juce::Array<double> voicePos;
    Marker dragging = Marker::None;
    bool startedExternalDrag = false;
    juce::Rectangle<int> wave;
    juce::Rectangle<int> dragHandle;
    float lastStart = -1, lastEnd = -1, lastLoop = -1;
};
}
