#include "PreviewPlayer.h"

namespace sb
{
void PreviewPlayer::play (std::shared_ptr<PreviewClip> c)
{
    {
        const juce::SpinLock::ScopedLockType sl (pathLock);
        path = c ? c->path : juce::String();
    }
    stopRequested = false;
    waiting = c != nullptr;
    incoming.post (std::move (c));
}

void PreviewPlayer::stop()
{
    {
        const juce::SpinLock::ScopedLockType sl (pathLock);
        path.clear();
    }
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

void PreviewPlayer::process (juce::AudioBuffer<float>& out, int start, int num,
                             const juce::AudioPlayHead::PositionInfo* info, SyncMode sync, float gain)
{
    // ---- fade-out tail of the previous clip (avoids clicks when switching) ----
    std::shared_ptr<PreviewClip> fresh;
    const bool gotNew = incoming.take (fresh);

    if (stopRequested.exchange (false) && ! gotNew)
    {
        trash.release (std::move (clip));
        playing = false; waiting = false; playheadSec = -1.0;
        startDelay = -1;
        return;
    }

    std::shared_ptr<PreviewClip> fading;
    juce::int64 fadingPos = 0;
    if (gotNew)
    {
        const bool keep = fresh && clip && fresh->keepPhase && fresh->path == clip->path && startDelay == 0;
        double norm = 0;
        if (keep && clip->audio.getNumSamples() > 0)
            norm = (double) pos / (double) (clip->loop && clip->loopLength > 0 ? clip->loopLength : clip->audio.getNumSamples());
        if (clip && startDelay == 0 && ! keep) { fading = clip; fadingPos = pos; }
        else trash.release (std::move (clip));
        if (keep) trash.release (std::move (clip));
        clip = std::move (fresh);

        if (clip == nullptr) { playing = false; waiting = false; startDelay = -1; }
        else if (keep)
        {
            const auto len = clip->loop && clip->loopLength > 0 ? clip->loopLength : clip->audio.getNumSamples();
            pos = (juce::int64) (norm * len);
            startDelay = 0;
        }
        else
        {
            pos = 0;
            startDelay = 0;
            const bool hostPlaying = info != nullptr && info->getIsPlaying() && info->getPpqPosition().hasValue()
                                     && info->getBpm().hasValue() && *info->getBpm() > 0;
            if (hostPlaying && sync != SyncMode::Immediate)
            {
                const double ppq = *info->getPpqPosition();
                const double bpm = *info->getBpm();
                const double samplesPerBeat = 60.0 / bpm * sr;
                int numer = 4, denom = 4;
                if (auto ts = info->getTimeSignature()) { numer = ts->numerator; denom = ts->denominator; }
                const double beatLen = 4.0 / denom;
                const double barLen = numer * beatLen;

                if (sync == SyncMode::InPhase && clip->tempoSynced && clip->loop && clip->loopLength > 0)
                {
                    // jump straight in at the loop position that matches the song position
                    const double barStart = info->getPpqPositionOfLastBarStart().orFallback (std::floor (ppq / barLen) * barLen);
                    const double loopBeats = clip->loopLength / samplesPerBeat;
                    const double intoBar = ppq - barStart;
                    // whole bars since an arbitrary reference, so multi-bar loops line up with bar 1
                    const double barsFromZero = std::round (barStart / barLen);
                    double phase = std::fmod (barsFromZero * barLen + intoBar, loopBeats);
                    if (phase < 0) phase += loopBeats;
                    pos = (juce::int64) (phase * samplesPerBeat);
                }
                else if (sync != SyncMode::InPhase)
                {
                    const double grid = sync == SyncMode::NextBar ? barLen : beatLen;
                    double target;
                    if (sync == SyncMode::NextBar)
                    {
                        const double barStart = info->getPpqPositionOfLastBarStart().orFallback (std::floor (ppq / barLen) * barLen);
                        target = barStart;
                        while (target < ppq - 1e-4) target += grid;
                    }
                    else
                        target = std::ceil (ppq / grid - 1e-4) * grid;
                    startDelay = juce::jmax<juce::int64> (0, (juce::int64) std::round ((target - ppq) * samplesPerBeat));
                }
            }
            playing = true;
        }
    }

    const int outCh = out.getNumChannels();

    if (fading != nullptr)
    {
        const int fadeLen = juce::jmin (num, 256);
        const auto& a = fading->audio;
        for (int i = 0; i < fadeLen; ++i)
        {
            auto p = fadingPos + i;
            if (fading->loop && fading->loopLength > 0) p %= fading->loopLength;
            if (p >= a.getNumSamples()) break;
            const float g = gain * (1.0f - (float) i / (float) fadeLen);
            for (int c = 0; c < outCh; ++c)
                out.addSample (c, start + i, a.getSample (juce::jmin (c, a.getNumChannels() - 1), (int) p) * g);
        }
        trash.release (std::move (fading));
    }

    if (clip == nullptr || startDelay < 0) { playheadSec = -1.0; return; }

    int i = 0;
    if (startDelay > 0)
    {
        const auto skip = (int) juce::jmin<juce::int64> (startDelay, num);
        startDelay -= skip;
        i = skip;
        waiting = startDelay > 0;
        if (startDelay > 0) { playheadSec = -1.0; return; }
    }
    waiting = false;

    const auto& a = clip->audio;
    const int len = a.getNumSamples();
    const int loopLen = clip->loop && clip->loopLength > 0 ? juce::jmin (clip->loopLength, len) : len;
    const int fadeIn = (pos == 0) ? 64 : 0;
    for (; i < num; ++i)
    {
        if (pos >= loopLen)
        {
            if (clip->loop && loopLen > 0) pos = 0;
            else
            {
                trash.release (std::move (clip));
                playing = false; playheadSec = -1.0;
                return;
            }
        }
        float g = gain;
        if (fadeIn > 0 && pos < fadeIn) g *= (float) pos / (float) fadeIn;
        for (int c = 0; c < outCh; ++c)
            out.addSample (c, start + i, a.getSample (juce::jmin (c, a.getNumChannels() - 1), (int) pos) * g);
        ++pos;
    }
    playheadSec = clip->sourceOffsetSec + (double) pos / sr * clip->sourceSpeed;
}
}
