#include "PreviewPlayer.h"

namespace sb
{
static constexpr int kFadeLen = 64;

void PreviewPlayer::play (std::shared_ptr<PreviewClip> c)
{
    {
        const juce::SpinLock::ScopedLockType sl (pathLock);
        path = c ? c->path : juce::String();
    }
    stopRequested = false;
    armed = c != nullptr;
    incoming.post (std::move (c));
}

void PreviewPlayer::stop()
{
    {
        const juce::SpinLock::ScopedLockType sl (pathLock);
        path.clear();
    }
    armed = false;
    stopRequested = true;
}

juce::String PreviewPlayer::currentPath() const
{
    const juce::SpinLock::ScopedLockType sl (pathLock);
    return path;
}

void PreviewPlayer::prepare (double sampleRate)
{
    sr = sampleRate;
}

void PreviewPlayer::renderFree (juce::AudioBuffer<float>& out, int start, int from, int to, float gain, bool wrap)
{
    if (! freePlaying || clip == nullptr) return;
    const auto& a = clip->audio;
    const int len = clip->loop && clip->loopLength > 0 ? juce::jmin (clip->loopLength, a.getNumSamples()) : a.getNumSamples();
    const int outCh = out.getNumChannels();
    for (int i = from; i < to; ++i)
    {
        if (freePos >= len)
        {
            if (wrap && len > 0) freePos = 0;
            else { freePlaying = false; return; }
        }
        float g = gain;
        if (fadeIn > 0) { g *= 1.0f - (float) fadeIn / (float) kFadeLen; --fadeIn; }
        for (int c = 0; c < outCh; ++c)
            out.addSample (c, start + i, a.getSample (juce::jmin (c, a.getNumChannels() - 1), (int) freePos) * g);
        ++freePos;
    }
}

void PreviewPlayer::process (juce::AudioBuffer<float>& out, int start, int num,
                             const juce::AudioPlayHead::PositionInfo* info, SyncMode sync, float gain)
{
    std::shared_ptr<PreviewClip> fresh;
    if (incoming.take (fresh))
    {
        const bool sameSound = fresh && clip && fresh->keepPhase && fresh->path == clip->path;
        trash.release (std::move (clip));
        clip = std::move (fresh);
        if (clip != nullptr && ! sameSound)
        {
            newlyArmed = true;
            playedSinceArm = false;
            freePlaying = false;
            freePos = 0;
            lockedActive = false;
        }
        else if (clip != nullptr && freePlaying)
        {
            // re-rendered for a new tempo: keep the same relative position
            freePos = juce::jmin<juce::int64> (freePos, clip->audio.getNumSamples());
        }
    }

    if (stopRequested.exchange (false))
    {
        trash.release (std::move (clip));
        freePlaying = false; lockedActive = false; newlyArmed = false;
    }

    const bool hostPlaying = info != nullptr && info->getIsPlaying() && info->getPpqPosition().hasValue()
                             && info->getBpm().hasValue() && *info->getBpm() > 0;
    hostRunning = hostPlaying;

    if (clip == nullptr || clip->audio.getNumSamples() == 0)
    {
        audible = false; playheadSec = -1.0;
        hostWasPlaying = hostPlaying;
        return;
    }

    // ---------------- Live stopped ----------------
    if (! hostPlaying)
    {
        if (hostWasPlaying)
        {
            // transport stopped: preview stops with it, ready to start again in time
            freePlaying = false; lockedActive = false; playedSinceArm = false;
        }
        hostWasPlaying = false;
        if (newlyArmed) { freePlaying = true; freePos = 0; fadeIn = kFadeLen; playedSinceArm = true; }
        newlyArmed = false;
        renderFree (out, start, 0, num, gain, clip->loop);
        audible = freePlaying;
        playheadSec = freePlaying ? clip->sourceOffsetSec + (double) freePos / sr * clip->sourceSpeed : -1.0;
        return;
    }

    // ---------------- Live playing ----------------
    if (! hostWasPlaying) { playedSinceArm = false; freePlaying = false; lockedActive = false; }
    hostWasPlaying = true;
    newlyArmed = false;

    const double ppq = *info->getPpqPosition();
    const double bpm = *info->getBpm();
    const double samplesPerBeat = 60.0 / bpm * sr;
    const double beatsPerSample = 1.0 / samplesPerBeat;
    const auto& a = clip->audio;
    const int outCh = out.getNumChannels();

    if (clip->tempoSynced && clip->loop && clip->loopBeats > 0 && clip->loopLength > 0)
    {
        // Locked to the song: sample index follows the song position every sample.
        const int loopLen = juce::jmin (clip->loopLength, a.getNumSamples());
        if (! lockedActive) fadeIn = kFadeLen;
        double idx = 0;
        for (int i = 0; i < num; ++i)
        {
            double phase = std::fmod (ppq + i * beatsPerSample, clip->loopBeats);
            if (phase < 0) phase += clip->loopBeats;
            idx = phase / clip->loopBeats * loopLen;
            const int i0 = juce::jlimit (0, loopLen - 1, (int) idx);
            float g = gain;
            if (fadeIn > 0) { g *= 1.0f - (float) fadeIn / (float) kFadeLen; --fadeIn; }
            for (int c = 0; c < outCh; ++c)
                out.addSample (c, start + i, a.getSample (juce::jmin (c, a.getNumChannels() - 1), i0) * g);
        }
        lockedActive = true;
        freePlaying = false;
        audible = true;
        playheadSec = clip->sourceOffsetSec + idx / sr * clip->sourceSpeed;
        return;
    }
    lockedActive = false;

    // Triggered: start on the next beat/bar (repeat on it when looping).
    int numer = 4, denom = 4;
    if (auto ts = info->getTimeSignature()) { numer = juce::jmax (1, ts->numerator); denom = juce::jmax (1, ts->denominator); }
    const double beatLen = 4.0 / denom;
    const double barLen = numer * beatLen;
    const double ppqEnd = ppq + num * beatsPerSample;

    juce::Array<int> triggers;
    if (sync == SyncMode::Immediate)
    {
        if (! playedSinceArm) triggers.add (0);
    }
    else
    {
        const double grid = sync == SyncMode::NextBeat ? beatLen : barLen;
        const double origin = sync == SyncMode::NextBeat ? 0.0
                            : info->getPpqPositionOfLastBarStart().orFallback (std::floor (ppq / barLen) * barLen);
        double b = origin + std::ceil ((ppq - origin) / grid - 1e-9) * grid;
        for (; b < ppqEnd && triggers.size() < 64; b += grid)
        {
            if (playedSinceArm && ! clip->retrigger) break;
            triggers.add (juce::jlimit (0, num - 1, (int) std::round ((b - ppq) * samplesPerBeat)));
            playedSinceArm = true;
        }
    }

    int pos = 0;
    for (int t : triggers)
    {
        renderFree (out, start, pos, t, gain, clip->loop && ! clip->retrigger);
        freePlaying = true; freePos = 0; fadeIn = juce::jmin (kFadeLen, 16);
        playedSinceArm = true;
        pos = t;
    }
    renderFree (out, start, pos, num, gain, clip->loop && ! clip->retrigger);
    audible = freePlaying;
    playheadSec = freePlaying ? clip->sourceOffsetSec + (double) freePos / sr * clip->sourceSpeed : -1.0;
}
}
