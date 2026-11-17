#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace sb
{
// A decoded sample held in memory at a known sample rate (always 1 or 2 channels).
struct LoadedSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100;
    juce::String path;
    int length() const { return audio.getNumSamples(); }
};

namespace loader
{
    // Decode a file and resample it to targetRate (if > 0).
    std::shared_ptr<LoadedSample> load (juce::AudioFormatManager& formats, const juce::File& file,
                                        double targetRate, double maxSeconds = 900.0);

    // Plain rate change: output length = input length / speed (pitch moves with speed).
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double speed);

    // Time-stretch keeping pitch: output length = input length * lengthRatio.
    juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& in, double sampleRate, double lengthRatio,
                                      double transposeSemitones = 0.0);

    // Write a buffer to a 24-bit WAV.
    bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& dest);
}
}
