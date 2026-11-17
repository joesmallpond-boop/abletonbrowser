#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ReleasePool.h"
#include <atomic>

namespace sb
{
enum class WarpMode : int { Off = 0, RePitch, Stretch };
enum class SyncMode : int { Immediate = 0, NextBeat, NextBar, InPhase };

// Audio ready to audition: already trimmed, resampled to the host rate and warped.
struct PreviewClip
{
    juce::AudioBuffer<float> audio;
    juce::String path;
    int loopLength = 0;          // samples; loop point when looping
    bool loop = false;
    bool tempoSynced = false;    // warped to the project tempo
    double renderedBpm = 0;      // project tempo it was rendered for
    double sourceOffsetSec = 0;  // where in the original file the clip starts
    double sourceSpeed = 1.0;    // original seconds per clip second (for the playhead in the waveform)
    bool keepPhase = false;      // replacing a clip of the same file: carry on from the same spot
};

// Plays the browser preview, starting on the next beat/bar when Live is playing.
class PreviewPlayer
{
public:
    void play (std::shared_ptr<PreviewClip> clip);  // message thread
    void stop();                                    // message thread

    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, int start, int num, const juce::AudioPlayHead::PositionInfo* pos,
                  SyncMode sync, float gain);
    void collectGarbage() { trash.clear(); }

    bool isPlaying() const { return playing.load(); }
    bool isWaiting() const { return waiting.load(); }
    juce::String currentPath() const;
    // playhead in seconds of the original file (for drawing), -1 if stopped
    double playheadSourceSec() const { return playheadSec.load(); }

private:
    Handoff<PreviewClip> incoming;
    ReleasePool<PreviewClip> trash;
    std::shared_ptr<PreviewClip> clip;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> playing { false }, waiting { false };
    std::atomic<double> playheadSec { -1.0 };
    double sr = 44100;
    juce::int64 pos = 0;
    juce::int64 startDelay = -1;  // samples until start; -1 = not scheduled
    float fade = 0;               // de-click ramp when stopping/switching
    mutable juce::SpinLock pathLock;
    juce::String path;
};
}
