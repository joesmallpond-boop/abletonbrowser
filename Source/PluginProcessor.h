#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Library/Library.h"
#include "Library/FilterModel.h"
#include "Audio/PreviewPlayer.h"
#include "Audio/SamplerEngine.h"

namespace sb
{
// How previews behave. Stored with the plugin state.
struct PreviewSettings
{
    WarpMode warp = WarpMode::Stretch;
    SyncMode sync = SyncMode::NextBar;
    bool loop = false;            // loop the preview
    bool autoPlay = true;         // audition when a sound is selected
    bool loadOnSelect = true;     // put the selected sound into the sampler
    bool trimSilence = true;      // previews start where the sound begins
    bool dragProcessed = false;   // drag the trimmed / tempo-matched render instead of the original file
};

class SampleBrowserProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    SampleBrowserProcessor();
    ~SampleBrowserProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ----- used by the editor (message thread) -----
    Library& getLibrary() { return *library; }
    juce::AudioProcessorValueTreeState& getState() { return apvts; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    PreviewPlayer& getPreview() { return preview; }
    SamplerEngine& getSampler() { return sampler; }

    PreviewSettings previewSettings;
    FilterState filterState;
    juce::String selectedPath;
    juce::var uiState; // splitter positions, sort column etc.

    void audition (const SampleInfo& info);      // play through the preview player
    void stopAudition();
    void loadIntoSampler (const SampleInfo& info);
    void reloadSamplerMapping (const SampleInfo& info); // root/onset changed by the user
    juce::String samplerPath() const;

    // File to hand to Live when dragging: the original, or a trimmed/tempo-matched render.
    juce::File fileForDrag (const SampleInfo& info, bool processed);

    double getHostBpm() const { return hostBpm.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    double getHostSampleRate() const { return currentSampleRate; }

    // Called when the settings changed (warp/sync) so the current preview is re-rendered.
    void previewSettingsChanged();
    void sendChangeMessageToEditor() { sendChangeMessage(); }

private:
    void timerCallback() override;
    std::shared_ptr<PreviewClip> renderClip (const SampleInfo& info, double bpmTarget, bool keepPhase);

    juce::SharedResourcePointer<Library> library;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    PreviewPlayer preview;
    SamplerEngine sampler;
    juce::AudioFormatManager formats;
    juce::ThreadPool renderPool { juce::ThreadPoolOptions{}.withThreadName ("SB render").withNumberOfThreads (2) };

    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    double currentSampleRate = 44100.0;
    std::atomic<float>* previewVolParam = nullptr;

    // last auditioned sample, to re-render on tempo/setting changes
    juce::CriticalSection auditionLock;
    std::unique_ptr<SampleInfo> auditioning;
    double auditionRenderedBpm = 0;
    std::atomic<int> auditionTicket { 0 }, samplerTicket { 0 };
    std::shared_ptr<PreviewClip> lastClip; // message-thread copy, reused for "processed" drags

    juce::String pendingSamplerPath; // restored from state, loaded once the library is ready
    float pendingRoot = -1000; double pendingOnset = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SampleBrowserProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowserProcessor)
};
}
