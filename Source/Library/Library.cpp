#include "Library.h"
#include "Analysis/Analyzer.h"
#include <unordered_map>

namespace sb
{
static constexpr juce::uint32 kMagic = 0x53424c31; // "SBL1"

Library::Library() : juce::Thread ("Bash Browser scanner")
{
    settings = new juce::DynamicObject();
    current = std::make_shared<const std::vector<SampleInfo>>();
    loadFromDisk();
    startTimer (2000);
    if (autoRescan && ! locations.isEmpty())
        rescan();
}

Library::~Library()
{
    stopTimer();
    cancelPendingUpdate();
    stopThread (8000);
    if (dirty) saveToDisk();
}

juce::File Library::getDataFolder() const
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    auto f = base.getChildFile ("Bash Browser");
    f.createDirectory();
    return f;
}

// ---------------------------------------------------------------- locations
juce::StringArray Library::getLocations() const
{
    const juce::ScopedLock sl (lock);
    return locations;
}

void Library::addLocation (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        if (! folder.isDirectory() || locations.contains (folder.getFullPathName())) return;
        locations.add (folder.getFullPathName());
    }
    saveSettings();
    sendChangeMessage();
    rescan();
}

void Library::removeLocation (const juce::String& path)
{
    {
        const juce::ScopedLock sl (lock);
        locations.removeString (path);
    }
    cancelScan();
    // drop that location's samples now
    std::vector<SampleInfo> kept;
    for (auto& s : *snapshot())
        if (s.root != path) kept.push_back (s);
    publish (std::move (kept));
    dirty = true;
    saveSettings();
    rescan();
}

void Library::setAutoRescan (bool b)
{
    autoRescan = b;
    saveSettings();
}

// ---------------------------------------------------------------- data
Library::Snapshot Library::snapshot() const
{
    const juce::ScopedLock sl (lock);
    return current;
}

void Library::publish (std::vector<SampleInfo>&& newSamples)
{
    {
        const juce::ScopedLock sl (lock);
        current = std::make_shared<const std::vector<SampleInfo>> (std::move (newSamples));
        rev++;
    }
    triggerAsyncUpdate();
}

void Library::handleAsyncUpdate()
{
    sendChangeMessage();
}

void Library::editSample (const juce::String& path, const std::function<void (SampleInfo&)>& edit)
{
    std::vector<SampleInfo> copy;
    {
        const juce::ScopedLock sl (lock);
        copy = *current;
    }
    bool changed = false;
    for (auto& s : copy)
        if (s.path == path) { edit (s); changed = true; break; }
    if (! changed) return;
    publish (std::move (copy));
    dirty = true;
}

void Library::reanalyse (const juce::String& path)
{
    {
        const juce::ScopedLock sl (lock);
        reanalyseQueue.addIfNotAlreadyThere (path);
    }
    rescan();
}

juce::var Library::getSetting (const juce::String& name, const juce::var& def) const
{
    const juce::ScopedLock sl (lock);
    return settings->hasProperty (name) ? settings->getProperty (name) : def;
}

void Library::setSetting (const juce::String& name, const juce::var& value)
{
    {
        const juce::ScopedLock sl (lock);
        settings->setProperty (name, value);
    }
    saveSettings();
}

// ---------------------------------------------------------------- scanning
void Library::rescan (bool reanalyseAll)
{
    if (isThreadRunning())
    {
        if (reanalyseAll) { signalThreadShouldExit(); stopThread (8000); }
        else return;
    }
    reanalyseAllRequested = reanalyseAll;
    if (reanalyseAll) projects.clear();
    ticksSinceScan = 0;
    startThread (juce::Thread::Priority::low);
}

void Library::cancelScan()
{
    signalThreadShouldExit();
    stopThread (8000);
}

void Library::getProgress (int& done, int& total, juce::String& status) const
{
    done = progressDone.load(); total = progressTotal.load();
    const juce::ScopedLock sl (lock);
    status = statusText;
}

void Library::run()
{
    scanning = true;
    progressDone = 0; progressTotal = 0;
    auto setStatus = [this] (const juce::String& s) { { const juce::ScopedLock sl (lock); statusText = s; } triggerAsyncUpdate(); };
    setStatus ("Looking for sounds...");

    juce::StringArray roots, forced;
    bool all;
    {
        const juce::ScopedLock sl (lock);
        roots = locations;
        forced = reanalyseQueue;
        reanalyseQueue.clear();
        all = reanalyseAllRequested;
    }

    auto start = snapshot();
    std::unordered_map<std::string, const SampleInfo*> existing;
    for (auto& s : *start) existing[s.path.toStdString()] = &s;

    struct Job { juce::File file, root; };
    std::vector<Job> toAnalyse;
    std::vector<SampleInfo> result;
    std::unordered_map<std::string, bool> seen;

    for (auto& r : roots)
    {
        juce::File root (r);
        if (! root.isDirectory())
        {
            // offline drive: keep what we already know about it
            for (auto& s : *start) if (s.root == r) { result.push_back (s); seen[s.path.toStdString()] = true; }
            continue;
        }
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, Analyzer::fileWildcard(),
                                                                juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
        {
            if (threadShouldExit()) break;
            auto f = entry.getFile();
            auto p = f.getFullPathName();
            if (p.contains ("/Backup/") || p.contains ("\\Backup\\") || p.contains ("Ableton Project Info")
                || f.getFileName().startsWithChar ('.') || entry.isHidden())
                continue;
            auto key = p.toStdString();
            if (seen.count (key)) continue;
            seen[key] = true;
            auto it = existing.find (key);
            bool unchanged = it != existing.end() && it->second->fileSize == entry.getFileSize()
                             && it->second->modTime == entry.getModificationTime().toMilliseconds();
            if (unchanged && ! all && ! forced.contains (p))
                result.push_back (*it->second);
            else
                toAnalyse.push_back ({ f, root });
        }
        if (threadShouldExit()) break;
    }

    if (threadShouldExit()) { scanning = false; setStatus ("Scan stopped"); return; }

    progressTotal = (int) toAnalyse.size();
    if (toAnalyse.empty())
    {
        // still publish so removed files disappear
        std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.path < b.path; });
        if (result.size() != start->size()) { publish (std::move (result)); dirty = true; }
        scanning = false;
        setStatus (juce::String (snapshot()->size()) + " sounds");
        return;
    }

    setStatus ("Analysing " + juce::String (toAnalyse.size()) + " sounds...");
    juce::CriticalSection resultLock;
    std::vector<SampleInfo> fresh;
    const int threads = juce::jlimit (1, 8, juce::SystemStats::getNumCpus() - 1);
    juce::ThreadPool pool (juce::ThreadPoolOptions{}.withThreadName ("SB analysis")
                                                      .withNumberOfThreads (threads)
                                                      .withDesiredThreadPriority (juce::Thread::Priority::low));
    const size_t batch = 8;
    for (size_t i = 0; i < toAnalyse.size(); i += batch)
    {
        pool.addJob ([&, i]
        {
            Analyzer analyzer;
            for (size_t j = i; j < juce::jmin (toAnalyse.size(), i + batch); ++j)
            {
                if (threadShouldExit()) return;
                SampleInfo info;
                bool ok = false;
                try { ok = analyzer.analyseFile (toAnalyse[j].file, toAnalyse[j].root, projects, info); }
                catch (...) { ok = false; }
                if (ok)
                {
                    const juce::ScopedLock sl (resultLock);
                    fresh.push_back (std::move (info));
                }
                ++progressDone;
            }
        });
    }

    auto merge = [&] (bool final)
    {
        std::vector<SampleInfo> merged = result;
        {
            const juce::ScopedLock sl (resultLock);
            merged.insert (merged.end(), fresh.begin(), fresh.end());
        }
        // carry over user edits from the live data (edits may have happened mid-scan)
        auto live = snapshot();
        std::unordered_map<std::string, const SampleInfo*> liveMap;
        for (auto& s : *live) liveMap[s.path.toStdString()] = &s;
        for (auto& s : merged)
        {
            auto it = liveMap.find (s.path.toStdString());
            if (it != liveMap.end()) s.copyUserFieldsFrom (*it->second);
        }
        if (! final)
        {
            // keep not-yet-analysed known samples visible during the scan
            std::unordered_map<std::string, bool> inMerged;
            for (auto& s : merged) inMerged[s.path.toStdString()] = true;
            for (auto& job : toAnalyse)
            {
                auto it = liveMap.find (job.file.getFullPathName().toStdString());
                if (it != liveMap.end() && ! inMerged.count (it->first)) merged.push_back (*it->second);
            }
        }
        std::sort (merged.begin(), merged.end(), [] (auto& a, auto& b) { return a.path < b.path; });
        publish (std::move (merged));
    };

    auto lastPublish = juce::Time::getMillisecondCounter();
    while (pool.getNumJobs() > 0)
    {
        if (threadShouldExit())
        {
            pool.removeAllJobs (true, 5000);
            break;
        }
        wait (250);
        auto now = juce::Time::getMillisecondCounter();
        if (now - lastPublish > 3000)
        {
            merge (false);
            lastPublish = now;
            setStatus ("Analysing " + juce::String (progressDone.load()) + " / " + juce::String (progressTotal.load()));
        }
    }

    merge (! threadShouldExit());
    dirty = true;
    scanning = false;
    setStatus (threadShouldExit() ? juce::String ("Scan stopped") : juce::String (snapshot()->size()) + " sounds");
}

void Library::timerCallback()
{
    if (dirty && ! scanning) { dirty = false; saveToDisk(); }
    // light rescan every 5 minutes picks up new recordings (only changed files are analysed)
    if (autoRescan && ++ticksSinceScan > 150 && ! isThreadRunning() && ! locations.isEmpty())
        rescan();
}

// ---------------------------------------------------------------- persistence
void Library::saveSettings()
{
    juce::DynamicObject::Ptr copy;
    {
        const juce::ScopedLock sl (lock);
        copy = settings->clone().release();
        juce::Array<juce::var> locs;
        for (auto& l : locations) locs.add (l);
        copy->setProperty ("locations", locs);
        copy->setProperty ("autoRescan", autoRescan);
    }
    getDataFolder().getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (copy.get())));
}

void Library::saveToDisk()
{
    auto snap = snapshot();
    juce::MemoryOutputStream mo;
    mo.writeInt ((int) kMagic);
    mo.writeInt (SampleInfo::serialVersion);
    mo.writeInt ((int) snap->size());
    for (auto& s : *snap) s.write (mo);
    auto file = getDataFolder().getChildFile ("library.sbl");
    auto temp = file.getSiblingFile ("library.sbl.tmp");
    if (temp.replaceWithData (mo.getData(), mo.getDataSize()))
        temp.moveFileTo (file);
}

void Library::loadFromDisk()
{
    auto folder = getDataFolder();
    auto parsed = juce::JSON::parse (folder.getChildFile ("settings.json"));
    if (auto* obj = parsed.getDynamicObject())
    {
        settings = obj->clone().release();
        if (auto* locs = obj->getProperty ("locations").getArray())
            for (auto& l : *locs) locations.addIfNotAlreadyThere (l.toString());
        autoRescan = obj->hasProperty ("autoRescan") ? (bool) obj->getProperty ("autoRescan") : true;
    }

    juce::FileInputStream in (folder.getChildFile ("library.sbl"));
    if (! in.openedOk()) return;
    if ((juce::uint32) in.readInt() != kMagic) return;
    int version = in.readInt();
    int count = in.readInt();
    std::vector<SampleInfo> samples;
    samples.reserve ((size_t) juce::jmax (0, count));
    for (int i = 0; i < count && ! in.isExhausted(); ++i)
    {
        SampleInfo s;
        if (s.read (in, version)) samples.push_back (std::move (s));
    }
    const juce::ScopedLock sl (lock);
    current = std::make_shared<const std::vector<SampleInfo>> (std::move (samples));
    rev++;
}
}
