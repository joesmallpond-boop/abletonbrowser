#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SampleLoader.h"
#include "ReleasePool.h"

namespace sb
{
// Sample loaded into the sampler, with the analysis info that drives mapping.
struct SamplerSample
{
    std::shared_ptr<LoadedSample> data;
    float rootMidi = 60.0f;   // note that plays the sample at its original pitch (fractional = cents)
    double autoStartSec = 0;  // where the sound begins (silence skipped)
    juce::String path;
};

// Parameter IDs (kept here so the processor, engine and UI agree)
namespace pid
{
    inline constexpr const char* mode = "mode";             // Classic / One-Shot
    inline constexpr const char* start = "start";           // 0..1 of file
    inline constexpr const char* end = "end";
    inline constexpr const char* autoStart = "autoStart";   // start from detected onset
    inline constexpr const char* loop = "loop";
    inline constexpr const char* loopStart = "loopStart";   // 0..1 of start..end region
    inline constexpr const char* loopFade = "loopFade";     // 0..0.5 of loop length
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* detune = "detune";
    inline constexpr const char* glide = "glide";
    inline constexpr const char* voices = "voices";
    inline constexpr const char* volume = "volume";
    inline constexpr const char* pan = "pan";
    inline constexpr const char* velVol = "velVol";
    inline constexpr const char* attack = "attack";
    inline constexpr const char* decay = "decay";
    inline constexpr const char* sustain = "sustain";
    inline constexpr const char* release = "release";
    inline constexpr const char* fadeIn = "fadeIn";         // one-shot
    inline constexpr const char* fadeOut = "fadeOut";
    inline constexpr const char* gate = "gate";             // one-shot: trigger / gate
    inline constexpr const char* filterOn = "filterOn";
    inline constexpr const char* filterType = "filterType";
    inline constexpr const char* cutoff = "cutoff";
    inline constexpr const char* reso = "reso";
    inline constexpr const char* filterEnv = "filterEnv";   // semitones
    inline constexpr const char* filterKey = "filterKey";
    inline constexpr const char* filterVel = "filterVel";
    inline constexpr const char* fAttack = "fAttack";
    inline constexpr const char* fDecay = "fDecay";
    inline constexpr const char* fSustain = "fSustain";
    inline constexpr const char* fRelease = "fRelease";
    inline constexpr const char* lfoWave = "lfoWave";
    inline constexpr const char* lfoRate = "lfoRate";
    inline constexpr const char* lfoSync = "lfoSync";
    inline constexpr const char* lfoSyncRate = "lfoSyncRate";
    inline constexpr const char* lfoRetrig = "lfoRetrig";
    inline constexpr const char* lfoPitch = "lfoPitch";
    inline constexpr const char* lfoFilter = "lfoFilter";
    inline constexpr const char* lfoVol = "lfoVol";
    inline constexpr const char* lfoPan = "lfoPan";
    inline constexpr const char* previewVol = "previewVol";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::StringArray lfoSyncRateNames();

// A Simpler-style sampler: one sample mapped across the keyboard from its detected root note.
class SamplerEngine
{
public:
    explicit SamplerEngine (juce::AudioProcessorValueTreeState& state);
    ~SamplerEngine();

    void setSample (std::shared_ptr<SamplerSample> s);   // message thread
    std::shared_ptr<SamplerSample> getSample() const;    // message thread copy
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double hostBpm);
    void allNotesOff();
    void collectGarbage() { trash.clear(); }

    // playback positions (seconds into the file) of active voices, for the waveform
    void getVoicePositions (juce::Array<double>& out) const;

    static constexpr int maxVoices = 32;

private:
    struct Params;
    struct Voice;

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void render (juce::AudioBuffer<float>& out, int start, int num, double hostBpm);

    juce::AudioProcessorValueTreeState& apvts;
    std::unique_ptr<Params> p;
    std::vector<std::unique_ptr<Voice>> voices;
    Handoff<SamplerSample> incoming;
    ReleasePool<SamplerSample> trash;
    std::shared_ptr<SamplerSample> sample;   // audio thread's copy
    mutable juce::SpinLock msgLock;
    std::shared_ptr<SamplerSample> msgSample; // message thread's copy
    double sr = 44100;
    juce::uint32 noteCounter = 0;
    double globalLfoPhase = 0;
    int lastNote = -1;
    juce::Array<int> heldNotes;               // for mono legato
    std::array<std::atomic<double>, maxVoices> voicePos;
};
}
