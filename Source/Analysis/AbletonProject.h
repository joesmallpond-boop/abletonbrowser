#pragma once
#include <juce_core/juce_core.h>

namespace sb
{
// Works out which Ableton project (if any) a sample lives in, and that project's tempo,
// by walking up the folders to find one containing a .als set.
struct ProjectInfo
{
    bool isProject = false;
    juce::File folder;
    juce::String name;      // folder name without " Project"
    juce::String setNames;  // names of the .als files (they often carry key/style)
    double tempo = 0;
};

class ProjectLocator
{
public:
    ProjectInfo find (const juce::File& sampleFile, const juce::File& scanRoot);
    static double readSetTempo (const juce::File& alsFile);
    void clear();

private:
    ProjectInfo examine (const juce::File& folder);
    juce::CriticalSection lock;
    std::map<juce::String, ProjectInfo> cache;
};
}
