#include "SamplerEngine.h"

namespace sb
{
using APVTS = juce::AudioProcessorValueTreeState;

juce::StringArray lfoSyncRateNames()
{
    return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/2 T", "1/4 T", "1/8 T", "1/16 T" };
}
static double lfoSyncBeats (int index)
{
    static const double beats[] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[juce::jlimit (0, 11, index)];
}

static juce::NormalisableRange<float> msRange (float maxMs, float centre)
{
    juce::NormalisableRange<float> r (0.0f, maxMs);
    r.setSkewForCentre (centre);
    return r;
}

APVTS::ParameterLayout createParameterLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto msAttr = AudioParameterFloatAttributes().withLabel ("ms")
        .withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? String (v / 1000.0f, 2) + " s" : String (v, v < 10 ? 1 : 0) + " ms"; });
    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + " %"; });
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto susAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
        return v <= 0.0001f ? String ("-inf dB") : String (Decibels::gainToDecibels (v), 1) + " dB"; });

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::mode, 1 }, "Mode", StringArray { "Classic", "One-Shot" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::start, 1 }, "Start", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::end, 1 }, "End", NormalisableRange<float> (0, 1), 1.0f, pctAttr));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::autoStart, 1 }, "Auto Start", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::loop, 1 }, "Loop", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::loopStart, 1 }, "Loop Position", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::loopFade, 1 }, "Loop Fade", NormalisableRange<float> (0, 0.5f), 0.05f, pctAttr));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { pid::transpose, 1 }, "Transpose", -48, 48, 0,
                                                AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::detune, 1 }, "Detune", NormalisableRange<float> (-50, 50), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ct"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::glide, 1 }, "Glide", msRange (2000, 150), 0.0f, msAttr));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { pid::voices, 1 }, "Voices", 1, SamplerEngine::maxVoices, 8));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::volume, 1 }, "Volume", NormalisableRange<float> (-36, 12), 0.0f, dbAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::pan, 1 }, "Pan", NormalisableRange<float> (-1, 1), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                                      int p = (int) std::round (v * 50); return p == 0 ? String ("C") : String (std::abs (p)) + (p < 0 ? "L" : "R"); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::velVol, 1 }, "Vel > Vol", NormalisableRange<float> (0, 1), 0.35f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::attack, 1 }, "Attack", msRange (20000, 300), 0.1f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::decay, 1 }, "Decay", msRange (60000, 1000), 600.0f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::sustain, 1 }, "Sustain", NormalisableRange<float> (0, 1, 0, 0.4f), 1.0f, susAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::release, 1 }, "Release", msRange (60000, 1000), 50.0f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fadeIn, 1 }, "Fade In", msRange (2000, 100), 0.0f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fadeOut, 1 }, "Fade Out", msRange (2000, 100), 0.1f, msAttr));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::gate, 1 }, "Gate", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::filterOn, 1 }, "Filter", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::filterType, 1 }, "Filter Type",
                                                   StringArray { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "BP 24" }, 0));
    NormalisableRange<float> freq (20.0f, 20000.0f); freq.setSkewForCentre (800.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::cutoff, 1 }, "Frequency", freq, 20000.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                                      return v >= 1000 ? String (v / 1000.0f, 2) + " kHz" : String (v, 0) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::reso, 1 }, "Resonance", NormalisableRange<float> (0, 1), 0.1f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::filterEnv, 1 }, "Filter Env", NormalisableRange<float> (-72, 72), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::filterKey, 1 }, "Key Track", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::filterVel, 1 }, "Vel > Freq", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fAttack, 1 }, "F Attack", msRange (20000, 300), 0.1f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fDecay, 1 }, "F Decay", msRange (60000, 1000), 600.0f, msAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fSustain, 1 }, "F Sustain", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::fRelease, 1 }, "F Release", msRange (60000, 1000), 50.0f, msAttr));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::lfoWave, 1 }, "LFO Wave",
                                                   StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" }, 0));
    NormalisableRange<float> rate (0.01f, 40.0f); rate.setSkewForCentre (2.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::lfoRate, 1 }, "LFO Rate", rate, 1.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1 ? 2 : 1) + " Hz"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::lfoSync, 1 }, "LFO Sync", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::lfoSyncRate, 1 }, "LFO Sync Rate", lfoSyncRateNames(), 4));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { pid::lfoRetrig, 1 }, "LFO Retrigger", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::lfoPitch, 1 }, "LFO > Pitch", NormalisableRange<float> (0, 12), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " st"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::lfoFilter, 1 }, "LFO > Filter", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::lfoVol, 1 }, "LFO > Volume", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::lfoPan, 1 }, "LFO > Pan", NormalisableRange<float> (0, 1), 0.0f, pctAttr));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::previewVol, 1 }, "Preview Volume", NormalisableRange<float> (-36, 6), -3.0f, dbAttr));
    return l;
}

// ------------------------------------------------------------------ params
struct SamplerEngine::Params
{
    explicit Params (APVTS& s)
    {
        auto get = [&] (const char* id) { auto* v = s.getRawParameterValue (id); jassert (v != nullptr); return v; };
        mode = get (pid::mode); start = get (pid::start); end = get (pid::end); autoStart = get (pid::autoStart);
        loop = get (pid::loop); loopStart = get (pid::loopStart); loopFade = get (pid::loopFade);
        transpose = get (pid::transpose); detune = get (pid::detune); glide = get (pid::glide); voices = get (pid::voices);
        volume = get (pid::volume); pan = get (pid::pan); velVol = get (pid::velVol);
        attack = get (pid::attack); decay = get (pid::decay); sustain = get (pid::sustain); release = get (pid::release);
        fadeIn = get (pid::fadeIn); fadeOut = get (pid::fadeOut); gate = get (pid::gate);
        filterOn = get (pid::filterOn); filterType = get (pid::filterType); cutoff = get (pid::cutoff); reso = get (pid::reso);
        filterEnv = get (pid::filterEnv); filterKey = get (pid::filterKey); filterVel = get (pid::filterVel);
        fAttack = get (pid::fAttack); fDecay = get (pid::fDecay); fSustain = get (pid::fSustain); fRelease = get (pid::fRelease);
        lfoWave = get (pid::lfoWave); lfoRate = get (pid::lfoRate); lfoSync = get (pid::lfoSync); lfoSyncRate = get (pid::lfoSyncRate);
        lfoRetrig = get (pid::lfoRetrig); lfoPitch = get (pid::lfoPitch); lfoFilter = get (pid::lfoFilter);
        lfoVol = get (pid::lfoVol); lfoPan = get (pid::lfoPan);
    }
    std::atomic<float> *mode, *start, *end, *autoStart, *loop, *loopStart, *loopFade, *transpose, *detune, *glide, *voices,
        *volume, *pan, *velVol, *attack, *decay, *sustain, *release, *fadeIn, *fadeOut, *gate,
        *filterOn, *filterType, *cutoff, *reso, *filterEnv, *filterKey, *filterVel, *fAttack, *fDecay, *fSustain, *fRelease,
        *lfoWave, *lfoRate, *lfoSync, *lfoSyncRate, *lfoRetrig, *lfoPitch, *lfoFilter, *lfoVol, *lfoPan;
};

// ------------------------------------------------------------------ voice
struct SamplerEngine::Voice
{
    bool active = false, released = false;
    int note = 0;
    float velocity = 1;
    juce::uint32 age = 0;
    double pos = 0;
    float currentNote = 0, targetNote = 0;
    juce::int64 samplesPlayed = 0;
    float endFade = 1.0f;         // short ramp when hitting the region end without loop
    float stealFade = 1.0f;       // >0 and dropping when stolen
    bool stealing = false;
    juce::ADSR amp, flt;
    juce::dsp::StateVariableTPTFilter<float> f1, f2;
    double lfoPhase = 0;
    float shValue = 0;
    std::shared_ptr<SamplerSample> smp;
};

static inline float hermite (const float* d, int len, double pos)
{
    int i = (int) pos;
    float t = (float) (pos - i);
    auto at = [&] (int k) { return d[juce::jlimit (0, len - 1, k)]; };
    float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

SamplerEngine::SamplerEngine (APVTS& s) : apvts (s), p (std::make_unique<Params> (s))
{
    for (int i = 0; i < maxVoices; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voicePos[(size_t) i] = -1.0;
    }
}

SamplerEngine::~SamplerEngine() = default;

void SamplerEngine::setSample (std::shared_ptr<SamplerSample> s)
{
    {
        const juce::SpinLock::ScopedLockType sl (msgLock);
        msgSample = s;
    }
    incoming.post (std::move (s));
}

std::shared_ptr<SamplerSample> SamplerEngine::getSample() const
{
    const juce::SpinLock::ScopedLockType sl (msgLock);
    return msgSample;
}

void SamplerEngine::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 };
    for (auto& v : voices)
    {
        v->amp.setSampleRate (sampleRate);
        v->flt.setSampleRate (sampleRate);
        v->f1.prepare (spec); v->f2.prepare (spec);
    }
}

void SamplerEngine::allNotesOff()
{
    for (auto& v : voices) if (v->active) { v->released = true; v->amp.noteOff(); v->flt.noteOff(); }
    heldNotes.clear();
}

void SamplerEngine::getVoicePositions (juce::Array<double>& out) const
{
    out.clearQuick();
    for (auto& vp : voicePos) { double v = vp.load(); if (v >= 0) out.add (v); }
}

void SamplerEngine::noteOn (int note, float velocity)
{
    if (sample == nullptr || sample->data == nullptr) return;
    const int maxPoly = juce::jlimit (1, maxVoices, (int) p->voices->load());
    const float glideMs = p->glide->load();
    const float startNote = (glideMs > 0 && lastNote >= 0) ? (float) lastNote : (float) note;

    // Mono legato: reuse the sounding voice and glide
    if (maxPoly == 1)
    {
        heldNotes.removeFirstMatchingValue (note);
        heldNotes.add (note);
        for (auto& v : voices)
            if (v->active && ! v->released && ! v->stealing && v->smp == sample)
            {
                v->note = note; v->targetNote = (float) note;
                if (glideMs <= 0) v->currentNote = (float) note;
                lastNote = note;
                return;
            }
    }

    int active = 0;
    for (auto& v : voices) if (v->active && ! v->stealing) ++active;
    Voice* voice = nullptr;
    for (auto& v : voices) if (! v->active) { voice = v.get(); break; }
    if (active >= maxPoly || voice == nullptr)
    {
        // steal the oldest (released voices first)
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v->active && ! v->stealing && (oldest == nullptr || (v->released && ! oldest->released)
                                               || (v->released == oldest->released && v->age < oldest->age)))
                oldest = v.get();
        if (oldest != nullptr) { oldest->stealing = true; }
        if (voice == nullptr)
        {
            // no free slot: hard-reuse the oldest
            voice = oldest;
            if (voice == nullptr) return;
            voice->stealing = false;
            trash.release (std::move (voice->smp));
        }
    }

    auto& v = *voice;
    trash.release (std::move (v.smp));
    v.smp = sample;
    v.active = true; v.released = false; v.stealing = false; v.stealFade = 1.0f; v.endFade = 1.0f;
    v.note = note; v.velocity = velocity; v.age = ++noteCounter;
    v.currentNote = startNote; v.targetNote = (float) note;
    v.samplesPlayed = 0;

    const auto& d = *sample->data;
    const double len = d.length();
    double startFrac = p->start->load();
    double startPos = startFrac * len;
    if (p->autoStart->load() > 0.5f && startFrac <= 0.0001)
        startPos = sample->autoStartSec * d.sampleRate;
    v.pos = juce::jlimit (0.0, juce::jmax (0.0, len - 1), startPos);

    v.amp.setParameters ({ p->attack->load() / 1000.0f, p->decay->load() / 1000.0f, p->sustain->load(), p->release->load() / 1000.0f });
    v.flt.setParameters ({ p->fAttack->load() / 1000.0f, p->fDecay->load() / 1000.0f, p->fSustain->load(), p->fRelease->load() / 1000.0f });
    v.amp.reset(); v.flt.reset();
    v.amp.noteOn(); v.flt.noteOn();
    v.f1.reset(); v.f2.reset();
    v.lfoPhase = p->lfoRetrig->load() > 0.5f ? 0.0 : globalLfoPhase;
    v.shValue = juce::Random::getSystemRandom().nextFloat() * 2 - 1;
    lastNote = note;
    if (maxPoly == 1) { heldNotes.removeFirstMatchingValue (note); heldNotes.add (note); }
}

void SamplerEngine::noteOff (int note)
{
    const bool oneShot = p->mode->load() > 0.5f;
    const bool gate = p->gate->load() > 0.5f;
    const int maxPoly = juce::jlimit (1, maxVoices, (int) p->voices->load());

    if (maxPoly == 1)
    {
        heldNotes.removeFirstMatchingValue (note);
        if (! heldNotes.isEmpty())
        {
            // fall back to the previous held note (legato)
            int back = heldNotes.getLast();
            for (auto& v : voices)
                if (v->active && ! v->released && v->note == note)
                {
                    v->note = back; v->targetNote = (float) back;
                    if (p->glide->load() <= 0) v->currentNote = (float) back;
                }
            lastNote = back;
            return;
        }
    }

    for (auto& v : voices)
        if (v->active && ! v->released && v->note == note)
        {
            if (oneShot && ! gate) continue; // trigger mode plays to the end
            v->released = true;
            v->amp.noteOff(); v->flt.noteOff();
        }
}

void SamplerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double hostBpm)
{
    std::shared_ptr<SamplerSample> fresh;
    if (incoming.take (fresh))
    {
        trash.release (std::move (sample));
        sample = std::move (fresh);
        lastNote = -1;
    }

    int pos = 0;
    const int num = out.getNumSamples();
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, num, meta.samplePosition);
        if (t > pos) { render (out, pos, t - pos, hostBpm); pos = t; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
    }
    if (pos < num) render (out, pos, num - pos, hostBpm);
}

void SamplerEngine::render (juce::AudioBuffer<float>& out, int start, int num, double hostBpm)
{
    const bool oneShot = p->mode->load() > 0.5f;
    const bool loopOn = p->loop->load() > 0.5f && ! oneShot;
    const float transpose = p->transpose->load() + p->detune->load() / 100.0f;
    const float glideMs = p->glide->load();
    const float glideCoef = glideMs > 0 ? 1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sr * 0.25f)) : 1.0f;
    const float gainMaster = juce::Decibels::decibelsToGain (p->volume->load());
    const float panMaster = p->pan->load();
    const float velVol = p->velVol->load();
    const bool filterOn = p->filterOn->load() > 0.5f;
    const int ftype = (int) p->filterType->load();
    const float cutoff = p->cutoff->load();
    const float q = 0.5f * std::pow (20.0f, p->reso->load());
    const float fEnvAmt = p->filterEnv->load();
    const float fKey = p->filterKey->load();
    const float fVel = p->filterVel->load();
    const int wave = (int) p->lfoWave->load();
    double lfoHz = p->lfoRate->load();
    if (p->lfoSync->load() > 0.5f && hostBpm > 0)
        lfoHz = (hostBpm / 60.0) / lfoSyncBeats ((int) p->lfoSyncRate->load());
    const float lfoPitch = p->lfoPitch->load(), lfoFilter = p->lfoFilter->load();
    const float lfoVol = p->lfoVol->load(), lfoPan = p->lfoPan->load();
    const double fadeInSamp = p->fadeIn->load() * 0.001 * sr;
    const double fadeOutMs = p->fadeOut->load();
    const float startFrac = p->start->load(), endFrac = p->end->load();
    const float loopStartFrac = p->loopStart->load(), loopFadeFrac = p->loopFade->load();
    const bool autoStart = p->autoStart->load() > 0.5f;
    const double lfoInc = lfoHz / sr;
    const int stealLen = (int) (0.004 * sr);

    const auto filterType = (ftype == 2 || ftype == 3) ? juce::dsp::StateVariableTPTFilterType::highpass
                          : (ftype == 4 || ftype == 5) ? juce::dsp::StateVariableTPTFilterType::bandpass
                          : juce::dsp::StateVariableTPTFilterType::lowpass;
    const bool fourPole = ftype == 1 || ftype == 3 || ftype == 5;

    for (int vi = 0; vi < maxVoices; ++vi)
    {
        auto& v = *voices[(size_t) vi];
        if (! v.active || v.smp == nullptr || v.smp->data == nullptr) { voicePos[(size_t) vi] = -1.0; continue; }
        const auto& d = *v.smp->data;
        const int len = d.length();
        if (len < 2) { v.active = false; continue; }
        const float* L = d.audio.getReadPointer (0);
        const float* R = d.audio.getReadPointer (juce::jmin (1, d.audio.getNumChannels() - 1));

        // region
        double regionStart = startFrac * len;
        if (autoStart && startFrac <= 0.0001f) regionStart = v.smp->autoStartSec * d.sampleRate;
        double regionEnd = juce::jmax (regionStart + 2.0, (double) endFrac * len);
        regionEnd = juce::jmin (regionEnd, (double) len - 1);
        const double loopStartPos = regionStart + loopStartFrac * (regionEnd - regionStart);
        const double loopLen = juce::jmax (2.0, regionEnd - loopStartPos);
        const double xfade = loopFadeFrac * loopLen;
        const double rateBase = d.sampleRate / sr;

        v.f1.setType (filterType); v.f2.setType (filterType);
        v.f1.setResonance (q); v.f2.setResonance (fourPole ? 0.7071f : q);

        for (int i = 0; i < num; ++i)
        {
            // LFO
            float lfo = 0;
            const double ph = v.lfoPhase - std::floor (v.lfoPhase);
            switch (wave)
            {
                case 0: lfo = std::sin ((float) (ph * juce::MathConstants<double>::twoPi)); break;
                case 1: lfo = ph < 0.5 ? (float) (ph * 4 - 1) : (float) (3 - ph * 4); break;
                case 2: lfo = (float) (ph * 2 - 1); break;
                case 3: lfo = (float) (1 - ph * 2); break;
                case 4: lfo = ph < 0.5 ? 1.0f : -1.0f; break;
                default: lfo = v.shValue; break;
            }
            v.lfoPhase += lfoInc;
            if (v.lfoPhase >= 1.0) { v.lfoPhase -= 1.0; v.shValue = juce::Random::getSystemRandom().nextFloat() * 2 - 1; }

            // pitch / glide
            v.currentNote += (v.targetNote - v.currentNote) * glideCoef;
            const float semis = v.currentNote - v.smp->rootMidi + transpose + lfoPitch * lfo;
            const double inc = rateBase * std::exp2 (semis / 12.0);

            // sample read with loop crossfade
            float sl = hermite (L, len, v.pos), sr2 = hermite (R, len, v.pos);
            if (loopOn && xfade > 1 && v.pos > regionEnd - xfade)
            {
                const double into = (v.pos - (regionEnd - xfade)) / xfade;  // 0..1
                const double other = v.pos - loopLen;
                if (other >= 0)
                {
                    const float a = std::cos ((float) into * juce::MathConstants<float>::halfPi);
                    const float b = std::sin ((float) into * juce::MathConstants<float>::halfPi);
                    sl = sl * a + hermite (L, len, other) * b;
                    sr2 = sr2 * a + hermite (R, len, other) * b;
                }
            }

            // envelopes
            float env;
            if (oneShot)
            {
                env = 1.0f;
                const double played = v.pos - regionStart;
                if (fadeInSamp > 1 && played < fadeInSamp) env *= (float) (played / fadeInSamp);
                const double remainingSec = (regionEnd - v.pos) / d.sampleRate / std::max (1e-6, inc / rateBase);
                if (fadeOutMs > 0.05 && remainingSec * 1000.0 < fadeOutMs) env *= (float) (remainingSec * 1000.0 / fadeOutMs);
                if (v.released) env *= v.amp.getNextSample(); // gate mode uses the release time
                else v.amp.getNextSample();
            }
            else env = v.amp.getNextSample();
            const float fenv = v.flt.getNextSample();

            float vel = (1.0f - velVol) + velVol * v.velocity;
            float g = env * vel * v.endFade * gainMaster * (1.0f - lfoVol * 0.5f * (1.0f - lfo));
            if (v.stealing)
            {
                v.stealFade -= 1.0f / (float) juce::jmax (1, stealLen);
                if (v.stealFade <= 0) { v.active = false; break; }
                g *= v.stealFade;
            }

            if (filterOn)
            {
                if ((i & 15) == 0)
                {
                    float st = fEnvAmt * fenv + fKey * (v.currentNote - 60.0f) + lfoFilter * 48.0f * lfo
                               + fVel * (v.velocity - 1.0f) * 48.0f;
                    float fc = juce::jlimit (20.0f, (float) (sr * 0.45), cutoff * std::exp2 (st / 12.0f));
                    v.f1.setCutoffFrequency (fc);
                    v.f2.setCutoffFrequency (fc);
                }
                sl = v.f1.processSample (0, sl); sr2 = v.f1.processSample (1, sr2);
                if (fourPole) { sl = v.f2.processSample (0, sl); sr2 = v.f2.processSample (1, sr2); }
            }

            const float panPos = juce::jlimit (-1.0f, 1.0f, panMaster + lfoPan * lfo);
            const float angle = (panPos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            out.addSample (0, start + i, sl * g * std::cos (angle) * juce::MathConstants<float>::sqrt2);
            if (out.getNumChannels() > 1)
                out.addSample (1, start + i, sr2 * g * std::sin (angle) * juce::MathConstants<float>::sqrt2);

            // advance
            v.pos += inc;
            ++v.samplesPlayed;
            if (loopOn)
            {
                if (v.pos >= regionEnd) v.pos -= loopLen;
            }
            else if (v.pos >= regionEnd - 1)
            {
                v.endFade -= 0.01f; // ~2 ms ramp at the end
                if (v.endFade <= 0 || v.pos >= len - 1) { v.active = false; break; }
            }
            if (v.released && ! v.amp.isActive()) { v.active = false; break; }
        }
        if (! v.active) { trash.release (std::move (v.smp)); voicePos[(size_t) vi] = -1.0; }
        else voicePos[(size_t) vi] = v.pos / d.sampleRate;
    }
    globalLfoPhase += lfoInc * num;
    globalLfoPhase -= std::floor (globalLfoPhase);
}
}
