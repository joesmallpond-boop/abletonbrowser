#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ReleasePool.h"
#include <atomic>

namespace sb
{
enum class WarpMode : int { Off = 0, RePitch, Stretch };
// When one-shots (and loops with no tempo) start while Live is playing.
// Tempo-synced loops always lock to Live's song position.
enum class SyncMode : int { Immediate = 0, NextBeat, NextBar, InPhase };

// Audio ready to audition: already trimmed, resampled to the host rate and warped.
struct PreviewClip
{
    juce::AudioBuffer<float> audio;
    juce::String path;
    int loopLength = 0;          // samples; loop point when looping
    double loopBeats = 0;        // musical length of the loop (tempo-synced clips)
    bool loop = false;
    bool retrigger = false;      // one-shot repeated on every beat/bar while Live plays
    bool tempoSynced = false;    // warped to the project tempo
    double renderedBpm = 0;      // project tempo it was rendered for
    double sourceOffsetSec = 0;  // where in the original file the clip starts
    double sourceSpeed = 1.0;    // original seconds per clip second (for the playhead in the waveform)
    bool keepPhase = false;      // re-render of the same sound (tempo change): don't restart it
};

// The browser preview, slaved to Live's transport:
//  - Live playing: tempo-synced loops are locked to the song position (bar 1 = loop start), so they
//    stay in time for as long as they play and follow Live when it jumps. One-shots fire on the next
//    beat or bar (and repeat on it when Loop is on).
//  - Live stopped: a newly selected sound plays straight away, like Ableton's browser. When Live
//    stops, the preview stops with it; when Live starts again, the selected sound starts in time.
class PreviewPlayer
{
public:
    void play (std::shared_ptr<PreviewClip> clip);  // message thread: arm a sound
    void stop();                                    // message thread: disarm

    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, int start, int num, const juce::AudioPlayHead::PositionInfo* pos,
                  SyncMode sync, float gain);
    void collectGarbage() { trash.clear(); }

    bool isArmed() const { return armed.load(); }        // a sound is selected for preview
    bool isPlaying() const { return audible.load(); }    // and is currently sounding
    bool isWaiting() const { return armed.load() && ! audible.load() && hostRunning.load(); }
    juce::String currentPath() const;
    // playhead in seconds of the original file (for drawing), -1 if silent
    double playheadSourceSec() const { return playheadSec.load(); }

private:
    void renderFree (juce::AudioBuffer<float>& out, int start, int from, int to, float gain, bool wrap);

    Handoff<PreviewClip> incoming;
    ReleasePool<PreviewClip> trash;
    std::shared_ptr<PreviewClip> clip;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> armed { false }, audible { false }, hostRunning { false };
    std::atomic<double> playheadSec { -1.0 };
    double sr = 44100;

    bool hostWasPlaying = false;
    bool newlyArmed = false;
    bool freePlaying = false;     // playing from freePos (one-shots, or anything while Live is stopped)
    juce::int64 freePos = 0;
    bool playedSinceArm = false;
    bool lockedActive = false;    // was playing locked to the song last block
    int fadeIn = 0;               // de-click ramp remaining

    mutable juce::SpinLock pathLock;
    juce::String path;
};
}
