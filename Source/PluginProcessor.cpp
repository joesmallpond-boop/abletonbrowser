#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Audio/SampleLoader.h"

namespace sb
{
SampleBrowserProcessor::SampleBrowserProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      sampler (apvts)
{
    formats.registerBasicFormats();
    previewVolParam = apvts.getRawParameterValue (pid::previewVol);
    startTimerHz (15);
}

SampleBrowserProcessor::~SampleBrowserProcessor()
{
    stopTimer();
    renderPool.removeAllJobs (true, 4000);
}

bool SampleBrowserProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SampleBrowserProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;
    preview.prepare (sampleRate);
    sampler.prepare (sampleRate, samplesPerBlock);
    keyboardState.reset();
    if (rateChanged)
        juce::MessageManager::callAsync ([safe = juce::WeakReference<SampleBrowserProcessor> (this)] {
            if (safe != nullptr) safe->previewSettingsChanged();
        });
}

void SampleBrowserProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    juce::Optional<juce::AudioPlayHead::PositionInfo> info;
    if (auto* ph = getPlayHead()) info = ph->getPosition();
    if (info.hasValue())
    {
        if (auto b = info->getBpm()) if (*b > 0) hostBpm = *b;
        hostPlaying = info->getIsPlaying();
    }

    sampler.process (buffer, midi, hostBpm.load());
    const float pg = juce::Decibels::decibelsToGain (previewVolParam->load());
    preview.process (buffer, 0, n, info.hasValue() ? &*info : nullptr, previewSettings.sync, pg);
}

// ------------------------------------------------------------------ preview
std::shared_ptr<PreviewClip> SampleBrowserProcessor::renderClip (const SampleInfo& info, double target, bool keepPhase)
{
    auto loaded = loader::load (formats, juce::File (info.path), currentSampleRate, 600.0);
    if (loaded == nullptr) return nullptr;

    const auto settings = previewSettings;
    const bool isLoop = info.type() == SoundType::Loop;
    const double sr = loaded->sampleRate;
    double startSec = (settings.trimSilence && ! isLoop) ? info.onsetSec : 0.0;
    int startSample = juce::jlimit (0, juce::jmax (0, loaded->length() - 1), (int) (startSec * sr));

    juce::AudioBuffer<float> audio (loaded->audio.getNumChannels(), loaded->length() - startSample);
    for (int c = 0; c < audio.getNumChannels(); ++c)
        audio.copyFrom (c, 0, loaded->audio, c, startSample, audio.getNumSamples());

    auto clip = std::make_shared<PreviewClip>();
    clip->path = info.path;
    clip->sourceOffsetSec = startSample / sr;
    clip->keepPhase = keepPhase;
    clip->renderedBpm = target;

    double bpm = info.bpm();
    if (settings.warp != WarpMode::Off && bpm > 0 && target > 0)
    {
        // fold double/half-time so the stretch stays musical
        double best = bpm;
        for (double f : { 0.5, 2.0, 0.25, 4.0 })
            if (std::abs (std::log2 (bpm * f / target)) < std::abs (std::log2 (best / target))) best = bpm * f;
        bpm = best;
        const double lengthRatio = bpm / target;
        if (std::abs (lengthRatio - 1.0) > 1e-4)
        {
            if (settings.warp == WarpMode::Stretch) audio = loader::stretch (audio, sr, lengthRatio);
            else audio = loader::resample (audio, 1.0 / lengthRatio);
        }
        clip->tempoSynced = true;
        clip->sourceSpeed = 1.0 / lengthRatio;

        if (isLoop || settings.loop)
        {
            const double srcSec = (loaded->length() - startSample) / sr;
            const double beats = juce::jmax (1.0, std::round (srcSec * bpm / 60.0));
            clip->loopLength = juce::jmin (audio.getNumSamples(), (int) std::round (beats * 60.0 / target * sr));
        }
    }
    if (clip->loopLength <= 0) clip->loopLength = audio.getNumSamples();
    clip->loop = settings.loop;

    // short fade at the end to avoid a click
    const int fadeLen = juce::jmin (audio.getNumSamples(), (int) (0.003 * sr));
    if (! clip->loop && fadeLen > 0)
        audio.applyGainRamp (audio.getNumSamples() - fadeLen, fadeLen, 1.0f, 0.0f);
    clip->audio = std::move (audio);
    return clip;
}

void SampleBrowserProcessor::audition (const SampleInfo& info)
{
    {
        const juce::ScopedLock sl (auditionLock);
        auditioning = std::make_unique<SampleInfo> (info);
    }
    const int ticket = ++auditionTicket;
    const double target = hostBpm.load();
    auditionRenderedBpm = target;
    renderPool.addJob ([this, info, ticket, target, safe = juce::WeakReference<SampleBrowserProcessor> (this)]
    {
        if (ticket != auditionTicket.load()) return;
        auto clip = renderClip (info, target, false);
        if (ticket != auditionTicket.load() || clip == nullptr) return;
        juce::MessageManager::callAsync ([safe, clip, ticket] {
            if (safe == nullptr || ticket != safe->auditionTicket.load()) return;
            safe->lastClip = clip;
            safe->preview.play (clip);
        });
    });
}

void SampleBrowserProcessor::stopAudition()
{
    ++auditionTicket;
    {
        const juce::ScopedLock sl (auditionLock);
        auditioning.reset();
    }
    preview.stop();
}

void SampleBrowserProcessor::previewSettingsChanged()
{
    std::unique_ptr<SampleInfo> info;
    {
        const juce::ScopedLock sl (auditionLock);
        if (auditioning) info = std::make_unique<SampleInfo> (*auditioning);
    }
    if (info == nullptr || ! preview.isPlaying()) return;
    const int ticket = ++auditionTicket;
    const double target = hostBpm.load();
    auditionRenderedBpm = target;
    renderPool.addJob ([this, i = *info, ticket, target, safe = juce::WeakReference<SampleBrowserProcessor> (this)]
    {
        auto clip = renderClip (i, target, true);
        if (ticket != auditionTicket.load() || clip == nullptr) return;
        juce::MessageManager::callAsync ([safe, clip, ticket] {
            if (safe == nullptr || ticket != safe->auditionTicket.load()) return;
            safe->lastClip = clip;
            safe->preview.play (clip);
        });
    });
}

// ------------------------------------------------------------------ sampler
void SampleBrowserProcessor::loadIntoSampler (const SampleInfo& info)
{
    const int ticket = ++samplerTicket;
    const bool differentFile = samplerPath() != info.path;
    if (differentFile)
    {
        // a new sound starts with the full region, like Simpler
        for (auto* id : { pid::start, pid::end, pid::loopStart })
            if (auto* prm = apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->getDefaultValue());
    }
    renderPool.addJob ([this, info, ticket, safe = juce::WeakReference<SampleBrowserProcessor> (this)]
    {
        if (ticket != samplerTicket.load()) return;
        auto data = loader::load (formats, juce::File (info.path), 0.0, 900.0);
        if (data == nullptr || ticket != samplerTicket.load()) return;
        auto s = std::make_shared<SamplerSample>();
        s->data = data;
        s->path = info.path;
        s->rootMidi = info.rootMidi() > 0 ? info.rootMidi() : 60.0f;
        s->autoStartSec = info.onsetSec;
        juce::MessageManager::callAsync ([safe, s, ticket] {
            if (safe == nullptr || ticket != safe->samplerTicket.load()) return;
            safe->sampler.setSample (s);
            safe->sendChangeMessageToEditor();
        });
    });
}

void SampleBrowserProcessor::reloadSamplerMapping (const SampleInfo& info)
{
    auto cur = sampler.getSample();
    if (cur == nullptr || cur->path != info.path) return;
    auto s = std::make_shared<SamplerSample> (*cur);
    s->rootMidi = info.rootMidi() > 0 ? info.rootMidi() : 60.0f;
    s->autoStartSec = info.onsetSec;
    sampler.setSample (s);
}

juce::String SampleBrowserProcessor::samplerPath() const
{
    auto s = sampler.getSample();
    return s ? s->path : juce::String();
}

// ------------------------------------------------------------------ drag
juce::File SampleBrowserProcessor::fileForDrag (const SampleInfo& info, bool processed)
{
    juce::File original (info.path);
    if (! processed) return original;

    const double target = hostBpm.load();
    std::shared_ptr<PreviewClip> clip = lastClip;
    if (clip == nullptr || clip->path != info.path || std::abs (clip->renderedBpm - target) > 0.01)
        clip = renderClip (info, target, false);
    if (clip == nullptr) return original;

    auto name = original.getFileNameWithoutExtension();
    if (clip->tempoSynced) name << " [" << juce::String (target, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " bpm]";
    else name << " [trim]";
    auto dest = library->getDataFolder().getChildFile ("Rendered").getChildFile (juce::File::createLegalFileName (name) + ".wav");

    juce::AudioBuffer<float> audio (clip->audio.getNumChannels(), clip->loop || clip->tempoSynced ? clip->loopLength : clip->audio.getNumSamples());
    for (int c = 0; c < audio.getNumChannels(); ++c)
        audio.copyFrom (c, 0, clip->audio, c, 0, audio.getNumSamples());
    if (! loader::writeWav (audio, currentSampleRate, dest)) return original;
    return dest;
}

// ------------------------------------------------------------------ timer
void SampleBrowserProcessor::timerCallback()
{
    preview.collectGarbage();
    sampler.collectGarbage();

    // re-render the playing preview when the project tempo moves
    const double bpm = hostBpm.load();
    if (previewSettings.warp != WarpMode::Off && preview.isPlaying() && auditionRenderedBpm > 0
        && std::abs (bpm / auditionRenderedBpm - 1.0) > 0.002)
        previewSettingsChanged();

    // restore the sampler's sound from a saved set
    if (pendingSamplerPath.isNotEmpty())
    {
        auto path = pendingSamplerPath;
        pendingSamplerPath.clear();
        SampleInfo info;
        bool found = false;
        for (auto& s : *library->snapshot()) if (s.path == path) { info = s; found = true; break; }
        if (! found)
        {
            info.path = path;
            if (pendingRoot > -999) info.userRootMidi = pendingRoot;
            info.onsetSec = pendingOnset;
        }
        if (juce::File (path).existsAsFile()) loadIntoSampler (info);
    }
}

// ------------------------------------------------------------------ state
void SampleBrowserProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    auto* b = xml->createNewChildElement ("Browser");
    b->setAttribute ("selected", selectedPath);
    if (auto s = sampler.getSample())
    {
        b->setAttribute ("samplerPath", s->path);
        b->setAttribute ("samplerRoot", s->rootMidi);
        b->setAttribute ("samplerOnset", s->autoStartSec);
    }
    b->setAttribute ("warp", (int) previewSettings.warp);
    b->setAttribute ("sync", (int) previewSettings.sync);
    b->setAttribute ("loop", previewSettings.loop);
    b->setAttribute ("autoPlay", previewSettings.autoPlay);
    b->setAttribute ("loadOnSelect", previewSettings.loadOnSelect);
    b->setAttribute ("trimSilence", previewSettings.trimSilence);
    b->setAttribute ("dragProcessed", previewSettings.dragProcessed);
    b->setAttribute ("filters", juce::JSON::toString (filterState.toVar(), true));
    b->setAttribute ("ui", juce::JSON::toString (uiState, true));
    copyXmlToBinary (*xml, destData);
}

void SampleBrowserProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    if (auto* b = xml->getChildByName ("Browser"))
    {
        selectedPath = b->getStringAttribute ("selected");
        pendingSamplerPath = b->getStringAttribute ("samplerPath");
        pendingRoot = (float) b->getDoubleAttribute ("samplerRoot", -1000);
        pendingOnset = b->getDoubleAttribute ("samplerOnset", 0);
        previewSettings.warp = (WarpMode) b->getIntAttribute ("warp", (int) WarpMode::Stretch);
        previewSettings.sync = (SyncMode) b->getIntAttribute ("sync", (int) SyncMode::NextBar);
        previewSettings.loop = b->getBoolAttribute ("loop", false);
        previewSettings.autoPlay = b->getBoolAttribute ("autoPlay", true);
        previewSettings.loadOnSelect = b->getBoolAttribute ("loadOnSelect", true);
        previewSettings.trimSilence = b->getBoolAttribute ("trimSilence", true);
        previewSettings.dragProcessed = b->getBoolAttribute ("dragProcessed", false);
        filterState.fromVar (juce::JSON::parse (b->getStringAttribute ("filters")));
        uiState = juce::JSON::parse (b->getStringAttribute ("ui"));
        xml->removeChildElement (b, true);
    }
    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    sendChangeMessageToEditor();
}

juce::AudioProcessorEditor* SampleBrowserProcessor::createEditor()
{
    return new SampleBrowserEditor (*this);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sb::SampleBrowserProcessor();
}
