#include "SampleLoader.h"
#include "signalsmith-stretch.h"

namespace sb::loader
{
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double speed)
{
    if (speed <= 0 || std::abs (speed - 1.0) < 1e-6) return in;
    const int inN = in.getNumSamples();
    const int outN = juce::jmax (1, (int) std::floor (inN / speed));
    juce::AudioBuffer<float> out (in.getNumChannels(), outN);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        juce::LagrangeInterpolator interp;
        // pad the input so the interpolator never reads past the end
        std::vector<float> padded ((size_t) inN + 8, 0.0f);
        std::copy (in.getReadPointer (c), in.getReadPointer (c) + inN, padded.begin());
        interp.process (speed, padded.data(), out.getWritePointer (c), outN, (int) padded.size(), 0);
    }
    return out;
}

std::shared_ptr<LoadedSample> load (juce::AudioFormatManager& formats, const juce::File& file, double targetRate, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    auto result = std::make_shared<LoadedSample>();
    result->path = file.getFullPathName();
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
    juce::AudioBuffer<float> raw (chans, len);
    reader->read (&raw, 0, len, 0, true, chans > 1);
    result->sampleRate = reader->sampleRate;
    if (targetRate > 0 && std::abs (targetRate - reader->sampleRate) > 0.5)
    {
        result->audio = resample (raw, reader->sampleRate / targetRate);
        result->sampleRate = targetRate;
    }
    else
        result->audio = std::move (raw);
    return result;
}

juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& in, double sampleRate, double lengthRatio, double semis)
{
    if (lengthRatio <= 0) return in;
    if (std::abs (lengthRatio - 1.0) < 1e-4 && std::abs (semis) < 1e-4) return in;

    const int chans = in.getNumChannels();
    const int inN = in.getNumSamples();
    const int outN = juce::jmax (1, (int) std::round (inN * lengthRatio));

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (chans, (float) sampleRate);
    if (std::abs (semis) > 1e-4) st.setTransposeSemitones ((float) semis);

    const int inLat = st.inputLatency(), outLat = st.outputLatency();

    // Feed: pre-roll of inputLatency samples via seek, then the rest, then silence to flush.
    std::vector<std::vector<float>> inBuf ((size_t) chans), outBuf ((size_t) chans);
    std::vector<float*> inPtr ((size_t) chans), outPtr ((size_t) chans);

    const int seekLen = juce::jmin (inLat, inN);
    for (int c = 0; c < chans; ++c)
    {
        inBuf[(size_t) c].assign (in.getReadPointer (c), in.getReadPointer (c) + seekLen);
        inPtr[(size_t) c] = inBuf[(size_t) c].data();
    }
    st.seek (inPtr.data(), seekLen, 1.0 / lengthRatio);

    const int remainingIn = inN - seekLen;
    const int padIn = inLat;                // silence so the processing time reaches the end
    const int totalIn = remainingIn + padIn;
    const int processOut = juce::jmax (1, (int) std::round (totalIn * lengthRatio));
    for (int c = 0; c < chans; ++c)
    {
        inBuf[(size_t) c].assign ((size_t) totalIn, 0.0f);
        if (remainingIn > 0)
            std::copy (in.getReadPointer (c) + seekLen, in.getReadPointer (c) + inN, inBuf[(size_t) c].begin());
        inPtr[(size_t) c] = inBuf[(size_t) c].data();
        outBuf[(size_t) c].assign ((size_t) (processOut + outLat), 0.0f);
        outPtr[(size_t) c] = outBuf[(size_t) c].data();
    }
    st.process (inPtr.data(), totalIn, outPtr.data(), processOut);
    std::vector<float*> flushPtr ((size_t) chans);
    for (int c = 0; c < chans; ++c) flushPtr[(size_t) c] = outBuf[(size_t) c].data() + processOut;
    st.flush (flushPtr.data(), outLat);

    // Output has outLat samples of pre-roll at the start: skip it.
    juce::AudioBuffer<float> out (chans, outN);
    out.clear();
    const int avail = processOut + outLat - outLat;
    for (int c = 0; c < chans; ++c)
        out.copyFrom (c, 0, outBuf[(size_t) c].data() + outLat, juce::jmin (outN, avail));
    return out;
}

bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& dest)
{
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (dest.createOutputStream());
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate,
                                                     (unsigned int) audio.getNumChannels(), 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}
}
