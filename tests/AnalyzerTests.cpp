// Analysis tests: synthesises sounds with known answers, writes them into a fake library
// (including a fake Ableton project), runs the analyser and checks the tags.
// Usage: AnalyzerTests            -> run synthetic tests
//        AnalyzerTests <folder>   -> analyse a real folder and print the tags
#include "Analysis/Analyzer.h"
#include "Analysis/NameParser.h"
#include "Audio/SampleLoader.h"
#include "Library/FilterModel.h"
#include <random>

using namespace sb;

static const double SR = 44100.0;
static std::mt19937 rng (1234);

static float noise() { static std::uniform_real_distribution<float> d (-1.0f, 1.0f); return d (rng); }
static double mtof (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

static float saw (double phase) { return (float) (2.0 * (phase - std::floor (phase + 0.5))); }

static void addTone (juce::AudioBuffer<float>& b, double startSec, double durSec, double midi, float gain,
                     double attack = 0.005, double release = 0.05, int harmonics = 8)
{
    int s0 = (int) (startSec * SR), n = (int) (durSec * SR);
    double f = mtof (midi);
    for (int i = 0; i < n && s0 + i < b.getNumSamples(); ++i)
    {
        double t = i / SR;
        double env = juce::jmin (1.0, t / attack) * juce::jmin (1.0, (durSec - t) / release);
        double v = 0;
        for (int h = 1; h <= harmonics; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * f * h * t) / h;
        for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, s0 + i, (float) (v * env * gain));
    }
}

static void addKick (juce::AudioBuffer<float>& b, double startSec, float gain = 0.9f)
{
    int s0 = (int) (startSec * SR); double phase = 0;
    for (int i = 0; i < (int) (0.45 * SR) && s0 + i < b.getNumSamples(); ++i)
    {
        double t = i / SR;
        double f = 48 + 110 * std::exp (-t * 30);
        phase += f / SR;
        float v = (float) (std::sin (2 * juce::MathConstants<double>::pi * phase) * std::exp (-t * 7) * gain);
        for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, s0 + i, v);
    }
}

static void addNoiseHit (juce::AudioBuffer<float>& b, double startSec, double decay, float gain, double hpHz, double lpHz)
{
    int s0 = (int) (startSec * SR);
    juce::IIRFilter hp, lp;
    hp.setCoefficients (juce::IIRCoefficients::makeHighPass (SR, hpHz));
    lp.setCoefficients (juce::IIRCoefficients::makeLowPass (SR, lpHz));
    for (int i = 0; i < (int) (decay * 6 * SR) && s0 + i < b.getNumSamples(); ++i)
    {
        double t = i / SR;
        float v = lp.processSingleSampleRaw (hp.processSingleSampleRaw (noise())) * (float) std::exp (-t / decay) * gain;
        for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, s0 + i, v);
    }
}

static void write (const juce::AudioBuffer<float>& b, const juce::File& f) { loader::writeWav (b, SR, f); }

static int failures = 0, checks = 0;
static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "   FAIL: " << what << "\n"; }
    else std::cout << "   ok:   " << what << "\n";
}

static void printInfo (const SampleInfo& s, const AnalysisDebug* d = nullptr)
{
    std::cout << juce::File (s.path).getFileName() << "\n"
              << "   instrument=" << instrumentName (s.instrument()) << (s.instrumentFromName ? " (name)" : "")
              << " conf=" << s.instrumentConfidence
              << " key=" << s.keyDisplay() << " conf=" << s.keyConfidence
              << " tonality=" << tonalityName (s.tonality)
              << " type=" << (s.type() == SoundType::Loop ? "loop" : "one-shot")
              << " bpm=" << s.bpm() << " root=" << theory::midiNoteName (s.rootMidi()) << " (" << s.rootMidi() << ")"
              << "\n   onset=" << s.onsetSec << " tail=" << s.tailSec << " len=" << s.lengthSec
              << " attackMs=" << s.attackMs << " decay=" << s.decaySec << " centroid=" << s.centroidHz
              << " flat=" << s.flatness << " project=" << s.project << " styles=" << s.styles().joinIntoString (",");
    juce::StringArray ch;
    for (int b = 0; b < chNumFlags; ++b) if (s.character & (1u << b)) ch.add (characterName (b));
    std::cout << " character=" << ch.joinIntoString (",") << "\n";
    if (d)
        std::cout << "   [voiced=" << d->voicedFraction << " std=" << d->pitchStdSemis << " notes=" << d->distinctNotes
                  << " poly=" << d->polyphony << " keyScore=" << d->keyScore << " rhythm=" << d->rhythmStrength
                  << " acfBpm=" << d->acfBpm << " onsetRate=" << d->onsetRate << " claps=" << d->clapPeaks
                  << " low=" << d->lowFraction << "]\n";
}

static void writeFakeAls (const juce::File& f, double tempo)
{
    juce::String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Ableton><LiveSet><Tracks/><MasterTrack><DeviceChain><Mixer>"
                       "<Tempo><LomId Value=\"0\" /><Manual Value=\"" + juce::String (tempo) + "\" /></Tempo>"
                       "</Mixer></DeviceChain></MasterTrack></LiveSet></Ableton>";
    juce::FileOutputStream fo (f);
    fo.setPosition (0); fo.truncate();
    juce::GZIPCompressorOutputStream gz (fo, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
    gz.writeText (xml, false, false, nullptr);
}

static void analyseFolder (const juce::File& folder)
{
    Analyzer a; ProjectLocator p;
    for (const auto& e : juce::RangedDirectoryIterator (folder, true, Analyzer::fileWildcard()))
    {
        SampleInfo s;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (a.analyseFile (e.getFile(), folder, p, s)) { printInfo (s); std::cout << "   (" << juce::Time::getMillisecondCounterHiRes() - t0 << " ms)\n"; }
    }
}

int main (int argc, char** argv)
{
    if (argc > 1) { analyseFolder (juce::File (juce::String (argv[1]))); return 0; }

    // ---- name parser ----
    std::cout << "Name parsing\n";
    auto k = names::keyFromText ("Bass Gm 120");
    check (k.found && k.root == 7 && k.mode == Minor, "Gm -> G minor");
    k = names::keyFromText ("pad_F#min_dreamy");
    check (k.found && k.root == 6 && k.mode == Minor, "F#min -> F# minor");
    k = names::keyFromText ("Keys Bb major");
    check (k.found && k.root == 10 && k.mode == Major, "Bb major");
    k = names::keyFromText ("FM Bell 3");
    check (! k.found, "FM is not a key");
    k = names::keyFromText ("Snare 4");
    check (! k.found, "no key in 'Snare 4'");
    check (names::explicitBpmFromText ("drums_124bpm_a") == 124, "124bpm");
    check (names::explicitBpmFromText ("Loop 90 BPM") == 90, "90 BPM");
    auto tok = names::tokenise ("BigKick01-OpenHat");
    check (tok.joinIntoString (",") == "big,kick,01,open,hat", "tokenise camelCase/digits: " + tok.joinIntoString (","));
    check (names::stylesFromText ("Gm dub techno").contains ("Dub"), "style dub");

    // ---- build fake library ----
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sb_test_lib");
    root.deleteRecursively();
    root.createDirectory();

    struct Case { juce::String file; std::function<juce::AudioBuffer<float>()> make; };
    std::vector<Case> cases;

    cases.push_back ({ "sound_001.wav", [] { juce::AudioBuffer<float> b (1, (int) (2.0 * SR)); b.clear(); addTone (b, 0, 1.9, 43, 0.5f, 0.004, 0.3, 12); return b; } });
    cases.push_back ({ "sound_002.wav", [] { juce::AudioBuffer<float> b (2, (int) (3.0 * SR)); b.clear();
        for (double m : { 50.0, 62.0, 65.0, 69.0 }) addTone (b, 0, 2.9, m, 0.18f, 0.01, 0.2, 6); return b; } });
    cases.push_back ({ "sound_003.wav", [] { juce::AudioBuffer<float> b (1, (int) (4.2 * SR)); b.clear();
        double notes[] = { 67, 69, 70, 72, 74, 70, 69, 67, 62, 67 }; for (int i = 0; i < 10; ++i) addTone (b, i * 0.4, 0.38, notes[i], 0.4f, 0.005, 0.05, 5); return b; } });
    cases.push_back ({ "sound_004.wav", [] { juce::AudioBuffer<float> b (1, (int) (0.5 * SR)); b.clear(); addKick (b, 0); return b; } });
    cases.push_back ({ "sound_005.wav", [] { juce::AudioBuffer<float> b (1, (int) (0.25 * SR)); b.clear(); addNoiseHit (b, 0, 0.02, 0.8f, 7000, 18000); return b; } });
    cases.push_back ({ "sound_006.wav", [] { juce::AudioBuffer<float> b (1, (int) (0.4 * SR)); b.clear();
        addNoiseHit (b, 0, 0.06, 0.6f, 1200, 9000); addTone (b, 0, 0.12, 55, 0.3f, 0.001, 0.08, 2); return b; } });
    cases.push_back ({ "sound_007.wav", [] { juce::AudioBuffer<float> b (1, (int) (0.4 * SR)); b.clear();
        for (int i = 0; i < 3; ++i) addNoiseHit (b, i * 0.011, 0.003, 0.7f, 900, 6000); addNoiseHit (b, 0.034, 0.05, 0.6f, 900, 6000); return b; } });
    cases.push_back ({ "sound_008.wav", [] { // drum loop 124 bpm, 2 bars
        double beat = 60.0 / 124.0; juce::AudioBuffer<float> b (2, (int) (8 * beat * SR)); b.clear();
        for (int i = 0; i < 8; ++i) { if (i % 2 == 0) addKick (b, i * beat); else addNoiseHit (b, i * beat, 0.06, 0.6f, 1200, 9000); }
        for (int i = 0; i < 16; ++i) addNoiseHit (b, i * beat / 2, 0.015, 0.35f, 7000, 18000);
        return b; } });
    cases.push_back ({ "sound_009.wav", [] { juce::AudioBuffer<float> b (2, (int) (4.0 * SR)); b.clear();
        for (double m : { 48.0, 60.0, 64.0, 67.0 }) addTone (b, 0, 3.9, m, 0.15f, 0.6, 0.8, 4); return b; } });
    cases.push_back ({ "sound_010.wav", [] { juce::AudioBuffer<float> b (1, (int) (1.3 * SR)); b.clear(); addTone (b, 0.3, 0.8, 57, 0.5f, 0.002, 0.6, 6); return b; } });
    cases.push_back ({ "Rhodes Chords Fm 92bpm.wav", [] { juce::AudioBuffer<float> b (2, (int) (2.0 * SR)); b.clear();
        for (double m : { 53.0, 56.0, 60.0 }) addTone (b, 0, 1.9, m, 0.2f, 0.005, 0.3, 5); return b; } });

    for (auto& c : cases) write (c.make(), root.getChildFile (c.file));

    // fake Ableton project at 132 bpm, a 4 bar bass recording in G minor
    auto proj = root.getChildFile ("Gm dub Project");
    proj.getChildFile ("Samples/Recorded").createDirectory();
    writeFakeAls (proj.getChildFile ("Gm dub.als"), 132.0);
    {
        double beat = 60.0 / 132.0; juce::AudioBuffer<float> b (1, (int) (16 * beat * SR)); b.clear();
        double notes[] = { 43, 43, 46, 43, 50, 48, 46, 41 };
        for (int i = 0; i < 16; ++i) addTone (b, i * beat, beat * 0.9, notes[i % 8], 0.5f, 0.003, 0.05, 10);
        write (b, proj.getChildFile ("Samples/Recorded/1-Audio 0001.wav"));
    }

    Analyzer analyzer; ProjectLocator projects;
    auto get = [&] (const juce::File& f, AnalysisDebug& dbg) {
        SampleInfo s;
        std::unique_ptr<juce::AudioFormatReader> r (analyzer.getFormatManager().createReaderFor (f));
        juce::AudioBuffer<float> buf ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
        auto pi = projects.find (f, root);
        auto hints = Analyzer::makeHints (f, root, pi);
        s.path = f.getFullPathName(); s.numChannels = (int) r->numChannels; s.lengthSec = r->lengthInSamples / r->sampleRate;
        s.projectBpm = pi.tempo; s.project = pi.isProject ? pi.name : root.getFileName();
        s.autoStyles = names::stylesFromText (f.getFileNameWithoutExtension() + " " + pi.name);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        Analyzer::analyseAudio (buf, r->sampleRate, hints, s, &dbg);
        printInfo (s, &dbg);
        std::cout << "   (" << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << " ms)\n";
        return s;
    };

    AnalysisDebug d;
    std::cout << "\nAnalysis\n";
    auto s = get (root.getChildFile ("sound_001.wav"), d);
    check (s.tonality == Tonality::SingleNote, "bass note is a single note");
    check (s.keyRoot() == 7, "bass note is G");
    check (std::abs (s.rootMidi() - 43) < 0.3, "bass root midi ~43");
    check (s.instrument() == Instrument::Bass, "bass note -> Bass");

    s = get (root.getChildFile ("sound_002.wav"), d);
    check (s.tonality == Tonality::Chords, "D minor chord -> Chords");
    check (s.keyRoot() == 2 && s.keyMode() == Minor, "D minor chord -> D min (or relative)");

    s = get (root.getChildFile ("sound_003.wav"), d);
    check (s.tonality == Tonality::Melody, "melody -> Melody");
    check ((s.keyRoot() == 7 && s.keyMode() == Minor) || (s.keyRoot() == 10 && s.keyMode() == Major), "melody in G minor (or Bb major)");

    s = get (root.getChildFile ("sound_004.wav"), d);
    check (s.instrument() == Instrument::Kick, "kick -> Kick");
    check (s.keyRoot() >= 0, "kick has a pitch");

    s = get (root.getChildFile ("sound_005.wav"), d);
    check (s.instrument() == Instrument::HiHat, "noise tick -> Hi-hat");
    check (s.keyRoot() < 0, "hat -> no key");

    s = get (root.getChildFile ("sound_006.wav"), d);
    check (s.instrument() == Instrument::Snare, "snare -> Snare");

    s = get (root.getChildFile ("sound_007.wav"), d);
    check (s.instrument() == Instrument::Clap, "clap -> Clap");

    s = get (root.getChildFile ("sound_008.wav"), d);
    check (s.type() == SoundType::Loop, "drum loop -> Loop");
    check (s.instrument() == Instrument::DrumLoop, "drum loop -> Drum loop");
    check (std::abs (s.bpm() - 124) < 1.0, "drum loop 124 bpm");

    s = get (root.getChildFile ("sound_009.wav"), d);
    check (s.instrument() == Instrument::Pad, "slow chord -> Pad");
    check (s.keyRoot() == 0 && s.keyMode() == Major, "pad C major");

    s = get (root.getChildFile ("sound_010.wav"), d);
    check (std::abs (s.onsetSec - 0.3) < 0.01, "auto start skips 300 ms of silence");
    check (std::abs (s.rootMidi() - 57) < 0.3, "pluck root A2 (midi 57)");

    s = get (root.getChildFile ("Rhodes Chords Fm 92bpm.wav"), d);
    check (s.instrument() == Instrument::Keys, "name 'Rhodes' -> Keys");
    check (s.keyRoot() == 5 && s.keyMode() == Minor, "name key Fm");
    check (s.bpm() == 92, "name bpm 92");

    s = get (proj.getChildFile ("Samples/Recorded/1-Audio 0001.wav"), d);
    check (s.project == "Gm dub", "project name from folder");
    check (std::abs (s.bpm() - 132) < 0.01, "project tempo 132 from .als");
    check (s.keyRoot() == 7 && s.keyMode() == Minor, "project key G minor");
    check (s.instrument() == Instrument::Bass, "low riff -> Bass");
    check (s.styles().contains ("Dub"), "style Dub from project name");

    // ---- full file path, incl. project lookup ----
    std::cout << "\nanalyseFile on project sample\n";
    SampleInfo full;
    check (analyzer.analyseFile (proj.getChildFile ("Samples/Recorded/1-Audio 0001.wav"), root, projects, full), "analyseFile ok");
    check (full.project == "Gm dub" && full.styles().contains ("Dub"), "analyseFile project + style");

    // ---- filter model ----
    std::cout << "\nFilters\n";
    std::vector<SampleInfo> lib;
    for (const auto& e : juce::RangedDirectoryIterator (root, true, Analyzer::fileWildcard()))
    {
        SampleInfo si;
        if (analyzer.analyseFile (e.getFile(), root, projects, si)) lib.push_back (si);
    }
    FilterModel fm;
    fm.rebuild (lib, 132.0, false);
    FilterState st;
    st.toggle (Category::Instrument, "Bass");
    st.toggle (Category::Key, "G min");
    auto res = fm.apply (lib, st);
    check (res.indices.size() == 2, "Bass + G min -> G min riff + G note (" + juce::String ((int) res.indices.size()) + ")");
    FilterState cm; cm.toggle (Category::Instrument, "Bass"); cm.toggle (Category::Key, "C min");
    res = fm.apply (lib, cm);
    check (res.indices.empty(), "Bass + C min exact -> 0");
    fm.rebuild (lib, 132.0, true);
    res = fm.apply (lib, cm);
    check (res.indices.size() >= 1, "Bass + C min compatible -> G note fits (" + juce::String ((int) res.indices.size()) + ")");
    FilterState t2; t2.toggle (Category::Tempo, "Fits project tempo");
    res = fm.apply (lib, t2);
    check (res.indices.size() == 1, "fits project tempo 132 -> 1");

    // ---- stretch ----
    std::cout << "\nStretch\n";
    juce::AudioBuffer<float> tone (2, (int) SR); tone.clear(); addTone (tone, 0, 1.0, 69, 0.5f);
    auto stretched = loader::stretch (tone, SR, 1.25);
    check (stretched.getNumSamples() == (int) std::round (SR * 1.25), "stretch length 1.25x");
    {
        NameHints h; SampleInfo si; si.lengthSec = 1.25; AnalysisDebug dd;
        Analyzer::analyseAudio (stretched, SR, h, si, &dd);
        check (std::abs (si.rootMidi() - 69) < 0.3, "stretch keeps pitch (" + juce::String (si.rootMidi()) + ")");
        float pk = stretched.getMagnitude (0, (int) (0.2 * SR), (int) (0.8 * SR));
        check (pk > 0.2f, "stretched audio not silent (peak " + juce::String (pk) + ")");
    }
    auto repitched = loader::resample (tone, 2.0);
    check (std::abs (repitched.getNumSamples() - SR / 2) < 2, "re-pitch halves length");

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
