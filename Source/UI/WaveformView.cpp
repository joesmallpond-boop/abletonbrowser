#include "WaveformView.h"
#include "LookAndFeel.h"
#include "Audio/SamplerEngine.h"

namespace sb
{
using namespace juce;

WaveformView::WaveformView (AudioFormatManager& formats, AudioProcessorValueTreeState& s)
    : apvts (s), thumb (256, formats, cache)
{
    thumb.addChangeListener (this);
    startTimerHz (30);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

WaveformView::~WaveformView()
{
    thumb.removeChangeListener (this);
}

void WaveformView::setSample (const SampleInfo* s)
{
    if (s == nullptr) { info.reset(); thumb.clear(); repaint(); return; }
    const bool same = info && info->path == s->path;
    info = std::make_unique<SampleInfo> (*s);
    if (! same) thumb.setSource (new FileInputSource (File (s->path)));
    repaint();
}

void WaveformView::setPlayheads (double p, const Array<double>& v, bool w)
{
    previewPos = p; voicePos = v; waiting = w;
}

void WaveformView::timerCallback()
{
    // repaint while something moves, or when the sampler markers change
    float s = apvts.getRawParameterValue (pid::start)->load(), e = apvts.getRawParameterValue (pid::end)->load();
    float l = apvts.getRawParameterValue (pid::loopStart)->load() + apvts.getRawParameterValue (pid::loop)->load() * 2;
    if (previewPos >= 0 || ! voicePos.isEmpty() || waiting || s != lastStart || e != lastEnd || l != lastLoop)
    {
        lastStart = s; lastEnd = e; lastLoop = l;
        repaint();
    }
}

void WaveformView::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    dragHandle = r.removeFromRight (78).withSizeKeepingCentre (72, juce::jmin (52, r.getHeight() - 8));
    r.removeFromRight (6);
    wave = r;
}

float WaveformView::xForSec (double s) const
{
    const double len = info ? juce::jmax (1e-6, info->lengthSec) : 1.0;
    return (float) (wave.getX() + wave.getWidth() * (s / len));
}

double WaveformView::secForX (float x) const
{
    const double len = info ? info->lengthSec : 1.0;
    return jlimit (0.0, len, (x - wave.getX()) / (double) juce::jmax (1, wave.getWidth()) * len);
}

double WaveformView::paramSec (const char* id) const
{
    return info ? apvts.getRawParameterValue (id)->load() * info->lengthSec : 0;
}

double WaveformView::regionStartSec() const
{
    if (! info) return 0;
    const bool autoStart = apvts.getRawParameterValue (pid::autoStart)->load() > 0.5f;
    const float s = apvts.getRawParameterValue (pid::start)->load();
    return (autoStart && s <= 0.0001f) ? info->onsetSec : s * info->lengthSec;
}

void WaveformView::setParamFromSec (const char* id, double sec)
{
    if (! info || info->lengthSec <= 0) return;
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) jlimit (0.0, 1.0, sec / info->lengthSec)));
}

WaveformView::Marker WaveformView::markerAt (float x) const
{
    if (! inSampler || ! info) return Marker::None;
    auto near = [&] (double sec) { return std::abs (xForSec (sec) - x) < 6.0f; };
    const double start = regionStartSec();
    const double end = paramSec (pid::end);
    if (apvts.getRawParameterValue (pid::loop)->load() > 0.5f)
    {
        const double ls = start + apvts.getRawParameterValue (pid::loopStart)->load() * (end - start);
        if (near (ls)) return Marker::LoopStart;
    }
    if (near (start)) return Marker::Start;
    if (near (end)) return Marker::End;
    return Marker::None;
}

void WaveformView::paint (Graphics& g)
{
    g.fillAll (colours::bg);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (wave.toFloat(), 4);

    // drag handle
    {
        auto h = dragHandle.toFloat();
        g.setColour (info ? colours::accent.withAlpha (0.9f) : colours::line);
        g.fillRoundedRectangle (h, 5);
        g.setColour (Colours::black);
        Path arrow;
        auto c = h.getCentre().translated (0, -7);
        arrow.addArrow ({ c.x, c.y - 8, c.x, c.y + 8 }, 3.0f, 10.0f, 7.0f);
        g.fillPath (arrow);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("DRAG TO LIVE", h.withTrimmedTop (h.getHeight() * 0.6f), Justification::centred);
    }

    if (! info)
    {
        g.setColour (colours::textDim);
        g.setFont (FontOptions (13.0f));
        g.drawText ("Select a sound", wave, Justification::centred);
        return;
    }

    const double len = info->lengthSec;
    const bool sampler = inSampler;
    const double start = sampler ? regionStartSec() : info->onsetSec;
    const double end = sampler ? paramSec (pid::end) : len;
    const bool loopOn = sampler && apvts.getRawParameterValue (pid::loop)->load() > 0.5f
                        && apvts.getRawParameterValue (pid::mode)->load() < 0.5f;

    // outside the region: dim
    auto inner = wave.reduced (0, 4);
    if (thumb.getTotalLength() > 0)
    {
        g.setColour (colours::wave.withAlpha (0.35f));
        thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 0.95f);
        g.saveState();
        g.reduceClipRegion (Rectangle<int> ((int) xForSec (start), inner.getY(), (int) (xForSec (end) - xForSec (start)) + 1, inner.getHeight()));
        g.setColour (colours::wave);
        thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 0.95f);
        g.restoreState();
    }

    // auto start (detected onset)
    g.setColour (colours::good.withAlpha (0.7f));
    float ox = xForSec (info->onsetSec);
    g.drawVerticalLine ((int) ox, (float) wave.getY(), (float) wave.getBottom());

    if (sampler)
    {
        auto marker = [&] (double sec, Colour c, const String& label, bool flagRight) {
            float x = xForSec (sec);
            g.setColour (c);
            g.fillRect (x - 1, (float) wave.getY(), 2.0f, (float) wave.getHeight());
            Path flag;
            if (flagRight) flag.addTriangle (x, (float) wave.getY(), x + 8, (float) wave.getY(), x, (float) wave.getY() + 8);
            else flag.addTriangle (x, (float) wave.getY(), x - 8, (float) wave.getY(), x, (float) wave.getY() + 8);
            g.fillPath (flag);
            g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText (label, Rectangle<float> (flagRight ? x + 3 : x - 43, (float) wave.getBottom() - 14, 40, 12),
                        flagRight ? Justification::left : Justification::right);
        };
        if (loopOn)
        {
            const double ls = start + apvts.getRawParameterValue (pid::loopStart)->load() * (end - start);
            g.setColour (colours::accent.withAlpha (0.12f));
            g.fillRect (Rectangle<float> (xForSec (ls), (float) wave.getY(), xForSec (end) - xForSec (ls), (float) wave.getHeight()));
            marker (ls, colours::accent.withAlpha (0.8f), "LOOP", true);
        }
        marker (start, colours::accent, "S", true);
        marker (end, colours::accent, "E", false);
    }

    // playheads
    g.setColour (colours::accent2);
    if (previewPos >= 0) g.fillRect (xForSec (previewPos) - 1, (float) wave.getY(), 2.0f, (float) wave.getHeight());
    g.setColour (colours::good);
    for (auto v : voicePos) g.fillRect (xForSec (v) - 0.5f, (float) wave.getY(), 1.5f, (float) wave.getHeight());

    // info line
    g.setFont (FontOptions (11.5f));
    g.setColour (colours::textDim);
    String line = info->fileName();
    line << "   " << String (info->sampleRate / 1000.0, 1) << " kHz  " << (info->numChannels > 1 ? "stereo" : "mono");
    if (info->onsetSec > 0.001) line << "   starts at " << String (info->onsetSec * 1000.0, 0) << " ms";
    if (waiting) line << "   waiting for the next beat/bar...";
    g.drawText (line, wave.reduced (8, 4).removeFromTop (14), Justification::topLeft, true);
    if (sampler)
    {
        g.setColour (colours::good);
        g.drawText ("IN SAMPLER", wave.reduced (8, 4).removeFromTop (14), Justification::topRight, false);
    }
}

void WaveformView::mouseMove (const MouseEvent& e)
{
    if (dragHandle.contains (e.getPosition())) setMouseCursor (MouseCursor::DraggingHandCursor);
    else if (markerAt ((float) e.x) != Marker::None) setMouseCursor (MouseCursor::LeftRightResizeCursor);
    else setMouseCursor (MouseCursor::PointingHandCursor);
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    startedExternalDrag = false;
    dragging = dragHandle.contains (e.getPosition()) ? Marker::None : markerAt ((float) e.x);
    if (dragging != Marker::None)
    {
        const char* id = dragging == Marker::Start ? pid::start : dragging == Marker::End ? pid::end : pid::loopStart;
        if (auto* p = apvts.getParameter (id)) p->beginChangeGesture();
        if (dragging == Marker::Start && apvts.getRawParameterValue (pid::autoStart)->load() > 0.5f)
        {
            // moving the start by hand takes over from the automatic start point
            const double onset = regionStartSec();
            if (auto* p = apvts.getParameter (pid::autoStart)) { p->beginChangeGesture(); p->setValueNotifyingHost (0); p->endChangeGesture(); }
            setParamFromSec (pid::start, onset);
        }
    }
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (! info) return;
    if (dragging == Marker::None)
    {
        if (! startedExternalDrag && e.getDistanceFromDragStart() > 5)
        {
            startedExternalDrag = true;
            if (onStartDrag) onStartDrag();
        }
        return;
    }
    const double sec = secForX ((float) e.x);
    const double start = regionStartSec(), end = paramSec (pid::end);
    if (dragging == Marker::Start) setParamFromSec (pid::start, jmin (sec, end - 0.002));
    else if (dragging == Marker::End) setParamFromSec (pid::end, jmax (sec, start + 0.002));
    else if (auto* p = apvts.getParameter (pid::loopStart))
        p->setValueNotifyingHost ((float) jlimit (0.0, 0.98, (sec - start) / jmax (1e-6, end - start)));
    repaint();
}

void WaveformView::mouseUp (const MouseEvent& e)
{
    if (dragging != Marker::None)
    {
        const char* id = dragging == Marker::Start ? pid::start : dragging == Marker::End ? pid::end : pid::loopStart;
        if (auto* p = apvts.getParameter (id)) p->endChangeGesture();
        dragging = Marker::None;
        return;
    }
    if (! startedExternalDrag && e.mouseWasClicked() && wave.contains (e.getPosition()) && onClickPlay)
        onClickPlay();
}
}
