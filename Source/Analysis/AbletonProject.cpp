#include "AbletonProject.h"

namespace sb
{
double ProjectLocator::readSetTempo (const juce::File& als)
{
    juce::FileInputStream raw (als);
    if (! raw.openedOk()) return 0;
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);

    // Only the start of the set is needed: the master tempo appears early in <MasterTrack>,
    // but read generously since big sets put many tracks first.
    juce::MemoryBlock mb;
    const size_t limit = 64 * 1024 * 1024;
    char buf[65536];
    while (mb.getSize() < limit)
    {
        auto n = gz.read (buf, sizeof (buf));
        if (n <= 0) break;
        mb.append (buf, (size_t) n);
    }
    auto xml = mb.toString();

    // Prefer the master track's tempo
    int searchFrom = juce::jmax (0, xml.indexOf ("<MasterTrack"));
    if (xml.indexOf ("<MainTrack") >= 0) searchFrom = xml.indexOf ("<MainTrack");
    int t = xml.indexOf (searchFrom, "<Tempo>");
    if (t < 0) t = xml.indexOf ("<Tempo>");
    if (t < 0) return 0;
    int m = xml.indexOf (t, "<Manual Value=\"");
    if (m < 0) return 0;
    m += 15;
    int e = xml.indexOfChar (m, '"');
    double v = xml.substring (m, e).getDoubleValue();
    return (v >= 20 && v <= 999) ? v : 0;
}

ProjectInfo ProjectLocator::examine (const juce::File& folder)
{
    ProjectInfo info;
    auto sets = folder.findChildFiles (juce::File::findFiles, false, "*.als");
    if (sets.isEmpty()) return info;

    info.isProject = true;
    info.folder = folder;
    info.name = folder.getFileName();
    if (info.name.endsWithIgnoreCase (" Project")) info.name = info.name.dropLastCharacters (8);

    juce::File newest;
    for (auto& s : sets)
    {
        info.setNames << s.getFileNameWithoutExtension() << " ";
        if (newest == juce::File() || s.getLastModificationTime() > newest.getLastModificationTime())
            newest = s;
    }
    info.tempo = readSetTempo (newest);
    return info;
}

ProjectInfo ProjectLocator::find (const juce::File& sampleFile, const juce::File& scanRoot)
{
    auto dir = sampleFile.getParentDirectory();
    for (int depth = 0; depth < 8; ++depth)
    {
        auto key = dir.getFullPathName();
        {
            const juce::ScopedLock sl (lock);
            auto it = cache.find (key);
            if (it != cache.end())
            {
                if (it->second.isProject) return it->second;
            }
            else
            {
                auto info = examine (dir);
                cache[key] = info;
                if (info.isProject) return info;
            }
        }
        if (dir == scanRoot || dir.isRoot()) break;
        auto parent = dir.getParentDirectory();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

void ProjectLocator::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
}
}
