// Audio engine tests: sampler pitch mapping / auto start, and preview bar sync.
#include "Audio/SamplerEngine.h"
#include "Audio/PreviewPlayer.h"

using namespace sb;

static int failures = 0, checks = 0;
static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) ++failures;
    std::cout << (ok ? "   ok:   " : "   FAIL: ") << what << "\n";
}

struct Dummy : juce::AudioProcessor
{
    Dummy() : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())),
              state (*this, nullptr, "P", createParameterLayout()) {}
    const juce::String getName() const override { return "d"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioProcessorValueTreeState state;
};

static double estimateFreq (const juce::AudioBuffer<float>& b, int from, int to, double sr)
{
    int crossings = 0; int first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) <= 0 && b.getSample (0, i) > 0) { if (first < 0) first = i; last = i; ++crossings; }
    if (crossings < 2) return 0;
    return (crossings - 1) * sr / (double) (last - first);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000;     // host rate differs from the sample's rate on purpose
    const double fileSr = 44100;

    std::cout << "Sampler\n";
    Dummy d;
    SamplerEngine engine (d.state);
    engine.prepare (sr, 512);

    // 0.25 s of silence then a 220 Hz (A2 = midi 57) tone, recorded at 44.1k
    auto data = std::make_shared<LoadedSample>();
    data->sampleRate = fileSr;
    data->audio.setSize (1, (int) (fileSr * 2.25));
    data->audio.clear();
    for (int i = (int) (fileSr * 0.25); i < data->audio.getNumSamples(); ++i)
        data->audio.setSample (0, i, 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 220.0 * (i / fileSr)));
    auto smp = std::make_shared<SamplerSample>();
    smp->data = data; smp->rootMidi = 57.0f; smp->autoStartSec = 0.25; smp->path = "test";
    engine.setSample (smp);

    auto render = [&] (int note, int samples, bool off = false) {
        juce::AudioBuffer<float> out (2, samples); out.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        if (off) midi.addEvent (juce::MidiMessage::noteOff (1, note), samples / 2);
        engine.process (out, midi, 120.0);
        return out;
    };

    auto out = render (69, (int) (sr * 0.5)); // A3 should be 440 Hz
    double f = estimateFreq (out, (int) (sr * 0.05), (int) (sr * 0.45), sr);
    check (std::abs (f - 440.0) < 2.0, "A3 plays 440 Hz from a 220 Hz root (got " + juce::String (f, 2) + ")");
    check (out.getMagnitude (0, 0, 200) > 0.05f, "auto start: sound begins immediately (silence skipped)");
    engine.allNotesOff();
    juce::AudioBuffer<float> tail (2, (int) sr); tail.clear(); engine.process (tail, {}, 120.0);

    out = render (57, (int) (sr * 0.5));
    f = estimateFreq (out, (int) (sr * 0.05), (int) (sr * 0.45), sr);
    check (std::abs (f - 220.0) < 1.5, "root note plays original pitch, rate-corrected 44.1k->48k (got " + juce::String (f, 2) + ")");
    engine.allNotesOff();
    tail.clear(); engine.process (tail, {}, 120.0);

    // auto start off -> starts with silence
    d.state.getParameter (pid::autoStart)->setValueNotifyingHost (0.0f);
    out = render (57, (int) (sr * 0.2));
    check (out.getMagnitude (0, 0, (int) (sr * 0.2)) < 1e-4f, "auto start off: leading silence is played");
    d.state.getParameter (pid::autoStart)->setValueNotifyingHost (1.0f);
    engine.allNotesOff();
    tail.clear(); engine.process (tail, {}, 120.0);

    // release: note off then silence after release time
    out = render (60, (int) (sr * 0.4), true);
    juce::AudioBuffer<float> after (2, (int) (sr * 0.3)); after.clear(); engine.process (after, {}, 120.0);
    check (after.getMagnitude (0, (int) (sr * 0.2), (int) (sr * 0.1)) < 1e-4f, "note off releases to silence");

    // transpose
    d.state.getParameter (pid::transpose)->setValueNotifyingHost (d.state.getParameter (pid::transpose)->convertTo0to1 (12));
    out = render (57, (int) (sr * 0.5));
    f = estimateFreq (out, (int) (sr * 0.05), (int) (sr * 0.45), sr);
    check (std::abs (f - 440.0) < 2.0, "transpose +12 (got " + juce::String (f, 2) + ")");
    engine.allNotesOff();

    std::cout << "\nPreview sync\n";
    PreviewPlayer player;
    player.prepare (sr);
    auto clip = std::make_shared<PreviewClip>();
    clip->audio.setSize (1, 1000);
    for (int i = 0; i < 1000; ++i) clip->audio.setSample (0, i, 0.5f);
    clip->path = "c";
    juce::AudioPlayHead::PositionInfo pos;
    pos.setIsPlaying (true);
    pos.setBpm (120.0);
    pos.setPpqPosition (3.5);
    pos.setPpqPositionOfLastBarStart (0.0);
    pos.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });

    auto firstSound = [&] (SyncMode mode) {
        player.play (clip);
        const int block = 512;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> o (2, block); o.clear();
            pos.setPpqPosition (3.5 + b * block / sr * 2.0);
            player.process (o, 0, block, &pos, mode, 1.0f);
            for (int i = 0; i < block; ++i) if (o.getSample (0, i) != 0) { player.stop(); juce::AudioBuffer<float> x (2, 16); player.process (x, 0, 16, &pos, mode, 1); return b * block + i; }
        }
        return -1;
    };
    int s = firstSound (SyncMode::NextBar);
    check (std::abs (s - (int) (0.25 * sr)) <= 1, "next bar: ppq 3.5 @120 starts after 0.25 s (" + juce::String (s) + " samples)");
    s = firstSound (SyncMode::Immediate);
    check (s <= 1, "immediate starts at once (" + juce::String (s) + ")");
    pos.setIsPlaying (false);
    s = firstSound (SyncMode::NextBar);
    check (s <= 1, "transport stopped: starts at once (" + juce::String (s) + ")");

    std::cout << "\nTransport lock\n";
    {
        PreviewPlayer lp;
        lp.prepare (sr);
        const double spb = 60.0 / 120.0 * sr;            // 24000 samples per beat
        auto loopClip = std::make_shared<PreviewClip>();
        const int loopLen = (int) (4 * spb);              // one bar
        loopClip->audio.setSize (1, loopLen);
        for (int i = 0; i < loopLen; ++i) loopClip->audio.setSample (0, i, (float) i / loopLen);
        loopClip->loop = true; loopClip->tempoSynced = true; loopClip->loopBeats = 4; loopClip->loopLength = loopLen;
        loopClip->path = "loop";

        juce::AudioPlayHead::PositionInfo p;
        p.setIsPlaying (true); p.setBpm (120.0); p.setPpqPositionOfLastBarStart (4.0);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        auto block = [&] (double ppq, bool playing, int n = 512) {
            p.setPpqPosition (ppq); p.setIsPlaying (playing);
            juce::AudioBuffer<float> o (1, n); o.clear();
            lp.process (o, 0, n, &p, SyncMode::NextBar, 1.0f);
            return o;
        };

        lp.play (loopClip);
        auto o = block (5.0, true);   // beat 2 of bar 2
        check (std::abs (o.getSample (0, 200) - (float) ((spb + 200) / loopLen)) < 1e-3f,
               "loop selected mid-song plays from the matching point (beat 2)");
        o = block (5.0 + 512 / spb, true);
        check (std::abs (o.getSample (0, 0) - (float) ((spb + 512) / loopLen)) < 1e-3f, "stays locked across blocks");
        o = block (13.0, true);       // Live jumped (loop brace / locator)
        check (std::abs (o.getSample (0, 200) - (float) ((spb + 200) / loopLen)) < 1e-3f, "follows Live when the song position jumps");
        o = block (13.5, false);
        check (o.getMagnitude (0, 0, 512) == 0.0f, "Live stops -> preview stops");
        o = block (0.0, true);        // space bar: Live starts from the top
        check (o.getSample (0, 0) == 0.0f && std::abs (o.getSample (0, 300) - (float) (300.0 / loopLen)) < 1e-3f,
               "Live starts -> loop starts on the one, in time");

        // tempo change: still locked to beats (loop rendered for 120 bpm, Live now at 90)
        p.setBpm (90.0);
        o = block (2.0, true);
        check (std::abs (o.getSample (0, 200) - (float) ((2.0 + 200 / (60.0 / 90.0 * sr)) / 4.0)) < 1e-3f, "stays on the beat after a tempo change");
        p.setBpm (120.0);

        // one-shot repeated on every beat while Live plays (Loop ticked)
        auto hit = std::make_shared<PreviewClip>();
        hit->audio.setSize (1, 400); for (int i = 0; i < 400; ++i) hit->audio.setSample (0, i, 1.0f);
        hit->loop = true; hit->retrigger = true; hit->path = "hit";
        lp.play (hit);
        juce::Array<int> starts;
        double ppq = 3.9;
        for (int b = 0; b < 120; ++b)
        {
            p.setPpqPosition (ppq); p.setIsPlaying (true);
            juce::AudioBuffer<float> ob (1, 512); ob.clear();
            lp.process (ob, 0, 512, &p, SyncMode::NextBeat, 1.0f);
            for (int i = 1; i < 512; ++i) if (ob.getSample (0, i) > 0 && ob.getSample (0, i - 1) == 0) starts.add ((int) std::round ((ppq + i / spb) * 100));
            ppq += 512 / spb;
        }
        check (starts.size() >= 2 && std::abs (starts[0] - 400) <= 1 && std::abs (starts[1] - 500) <= 1,
               "one-shot with Loop repeats on each beat (" + juce::String (starts.size()) + " hits)");
    }

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
