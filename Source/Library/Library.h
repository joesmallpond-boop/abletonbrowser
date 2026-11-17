#pragma once
#include <juce_events/juce_events.h>
#include "SampleInfo.h"
#include "Analysis/AbletonProject.h"
#include <atomic>

namespace sb
{
// The shared sound library: scan locations, analysed samples and user edits.
// One instance is shared by every plugin instance in the process.
class Library : public juce::ChangeBroadcaster,
                private juce::Thread,
                private juce::Timer,
                private juce::AsyncUpdater
{
public:
    Library();
    ~Library() override;

    // ----- scan locations -----
    juce::StringArray getLocations() const;
    void addLocation (const juce::File& folder);
    void removeLocation (const juce::String& path);

    // ----- scanning -----
    void rescan (bool reanalyseAll = false);
    void cancelScan();
    bool isScanning() const { return scanning.load(); }
    // progress as done / total, plus a status line
    void getProgress (int& done, int& total, juce::String& status) const;

    bool getAutoRescan() const { return autoRescan; }
    void setAutoRescan (bool shouldRescan);

    // ----- data access (message thread) -----
    // Snapshot of all samples. Cheap to copy (shared pointer); changes whenever revision() does.
    using Snapshot = std::shared_ptr<const std::vector<SampleInfo>>;
    Snapshot snapshot() const;
    juce::uint64 revision() const { return rev.load(); }

    // Edit the user fields of one sample (by path). Saved automatically.
    void editSample (const juce::String& path, const std::function<void (SampleInfo&)>& edit);
    void reanalyse (const juce::String& path);

    juce::File getDataFolder() const;

    // In-process settings shared across instances
    juce::var getSetting (const juce::String& name, const juce::var& defaultValue) const;
    void setSetting (const juce::String& name, const juce::var& value);

private:
    void run() override;
    void timerCallback() override;
    void handleAsyncUpdate() override;

    void loadFromDisk();
    void saveToDisk();
    void saveSettings();
    void publish (std::vector<SampleInfo>&& newSamples);

    mutable juce::CriticalSection lock;
    Snapshot current;
    std::atomic<juce::uint64> rev { 1 };
    juce::StringArray locations;
    juce::DynamicObject::Ptr settings;
    bool autoRescan = true;
    bool reanalyseAllRequested = false;
    juce::StringArray reanalyseQueue;

    std::atomic<bool> scanning { false };
    std::atomic<int> progressDone { 0 }, progressTotal { 0 };
    juce::String statusText;
    ProjectLocator projects;
    bool dirty = false;
    int ticksSinceScan = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Library)
};
}
