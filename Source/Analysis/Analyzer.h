#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library/SampleInfo.h"
#include "AbletonProject.h"

namespace sb
{
// Hints taken from file, folder and project names before audio analysis.
struct NameHints
{
    juce::StringArray fileTokens;
    juce::Array<juce::StringArray> folderTokens; // nearest folder first
    int fileKeyRoot = -1, fileKeyMode = Major;
    int projectKeyRoot = -1, projectKeyMode = Major;
    double explicitBpm = 0;
    juce::Array<double> bpmCandidates;
    double projectBpm = 0;
    bool saysLoop = false, saysOneShot = false;
};

// Detailed numbers from one analysis run, used by the tests and the UI's info panel.
struct AnalysisDebug
{
    float voicedFraction = 0, pitchStdSemis = 0, keyScore = 0, rhythmStrength = 0, acfBpm = 0;
    int distinctNotes = 0, polyphony = 0, clapPeaks = 0;
    float lowFraction = 0, airFraction = 0, onsetRate = 0;
};

class Analyzer
{
public:
    Analyzer();

    // Reads and analyses a file. Returns false if it isn't a readable audio file.
    bool analyseFile (const juce::File& file, const juce::File& scanRoot, ProjectLocator& projects, SampleInfo& out);

    // Analyses audio already in memory (used by analyseFile and by the tests).
    static void analyseAudio (const juce::AudioBuffer<float>& audio, double sampleRate, const NameHints& hints,
                              SampleInfo& out, AnalysisDebug* debug = nullptr);

    static NameHints makeHints (const juce::File& file, const juce::File& scanRoot, const ProjectInfo& project);

    juce::AudioFormatManager& getFormatManager() { return formats; }
    static juce::String fileWildcard();
    static bool isAudioFile (const juce::File& f);

    static constexpr double maxAnalysisSeconds = 60.0;

private:
    juce::AudioFormatManager formats;
};
}
