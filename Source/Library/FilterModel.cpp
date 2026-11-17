#include "FilterModel.h"
#include <unordered_map>

namespace sb
{
juce::String categoryName (Category c)
{
    switch (c)
    {
        case Category::Instrument: return "Instrument";
        case Category::Key:        return "Key";
        case Category::Tempo:      return "Tempo";
        case Category::Type:       return "Type";
        case Category::Tonality:   return "Pitch content";
        case Category::Register:   return "Register";
        case Category::Character:  return "Character";
        case Category::Length:     return "Length";
        case Category::Style:      return "Style";
        case Category::Tags:       return "My tags";
        case Category::Source:     return "Project / source";
        default: break;
    }
    return {};
}

static const juce::StringArray& tempoOptions()
{
    static juce::StringArray o { "Fits project tempo", "No tempo", "Under 80", "80-99", "100-119", "120-129", "130-149", "150+" };
    return o;
}
static const juce::StringArray& typeOptions()     { static juce::StringArray o { "One-shot", "Loop" }; return o; }
static const juce::StringArray& registerOptions() { static juce::StringArray o { "Sub", "Low", "Mid", "High" }; return o; }
static const juce::StringArray& lengthOptions()   { static juce::StringArray o { "Under 0.25 s", "0.25-1 s", "1-4 s", "4-15 s", "Over 15 s" }; return o; }

const juce::StringArray& FilterModel::keyOptions()
{
    static juce::StringArray o = [] {
        juce::StringArray a;
        for (int mode = 0; mode < 2; ++mode)
            for (int r = 0; r < 12; ++r) a.add (theory::keyName (r, mode));
        a.add ("No key");
        return a;
    }();
    return o;
}

juce::StringArray FilterModel::memberships (const SampleInfo& s, Category c, double hostBpm, bool compatible)
{
    juce::StringArray m;
    switch (c)
    {
        case Category::Instrument: m.add (instrumentName (s.instrument())); break;
        case Category::Key:
        {
            int root = s.keyRoot(), mode = s.keyMode();
            if (root < 0) { m.add ("No key"); break; }
            if (mode == NoteOnly)
            {
                for (int km = 0; km < 2; ++km)
                    for (int kr = 0; kr < 12; ++kr)
                        if (compatible ? theory::scaleContains (kr, km, root) : kr == root)
                            m.add (theory::keyName (kr, km));
            }
            else
            {
                m.add (theory::keyName (root, mode));
                if (compatible) m.add (theory::keyName (theory::relativeKeyRoot (root, mode), 1 - mode));
            }
            break;
        }
        case Category::Tempo:
        {
            double b = s.bpm();
            if (b <= 0) { m.add ("No tempo"); break; }
            if (b < 80) m.add ("Under 80");
            else if (b < 100) m.add ("80-99");
            else if (b < 120) m.add ("100-119");
            else if (b < 130) m.add ("120-129");
            else if (b < 150) m.add ("130-149");
            else m.add ("150+");
            if (hostBpm > 0)
                for (double f : { 1.0, 2.0, 0.5 })
                    if (std::abs (b * f / hostBpm - 1.0) < 0.03) { m.add ("Fits project tempo"); break; }
            break;
        }
        case Category::Type: m.add (s.type() == SoundType::Loop ? "Loop" : "One-shot"); break;
        case Category::Tonality: m.add (tonalityName (s.tonality)); break;
        case Category::Register:
        {
            float p = s.rootMidi() > 0 ? s.rootMidi() : s.medianPitchMidi;
            if (s.tonality == Tonality::Atonal || p <= 0) break;
            m.add (p < 36 ? "Sub" : p < 52 ? "Low" : p < 72 ? "Mid" : "High");
            break;
        }
        case Category::Character:
            for (int b = 0; b < chNumFlags; ++b) if (s.character & (1u << b)) m.add (characterName (b));
            break;
        case Category::Length:
        {
            double l = s.effectiveLength();
            m.add (l < 0.25 ? "Under 0.25 s" : l < 1 ? "0.25-1 s" : l < 4 ? "1-4 s" : l < 15 ? "4-15 s" : "Over 15 s");
            break;
        }
        case Category::Style: m = s.styles(); break;
        case Category::Tags: m = s.userTags; break;
        case Category::Source: if (s.project.isNotEmpty()) m.add (s.project); break;
        default: break;
    }
    return m;
}

void FilterModel::rebuild (const std::vector<SampleInfo>& samples, double hostBpm, bool compatibleKeys)
{
    opts[(size_t) Category::Instrument].clear();
    for (int i = 1; i < (int) Instrument::Count; ++i) opts[(size_t) Category::Instrument].add (instrumentName ((Instrument) i));
    opts[(size_t) Category::Instrument].add (instrumentName (Instrument::Unknown));
    opts[(size_t) Category::Key] = keyOptions();
    opts[(size_t) Category::Tempo] = tempoOptions();
    opts[(size_t) Category::Type] = typeOptions();
    opts[(size_t) Category::Tonality].clear();
    for (int i = 0; i < (int) Tonality::Count; ++i) opts[(size_t) Category::Tonality].add (tonalityName ((Tonality) i));
    opts[(size_t) Category::Register] = registerOptions();
    opts[(size_t) Category::Character].clear();
    for (int b = 0; b < chNumFlags; ++b) opts[(size_t) Category::Character].add (characterName (b));
    opts[(size_t) Category::Length] = lengthOptions();

    // dynamic categories
    std::set<juce::String> styles, tags, sources;
    for (auto& s : samples)
    {
        for (auto& st : s.styles()) styles.insert (st);
        for (auto& t : s.userTags) tags.insert (t);
        if (s.project.isNotEmpty()) sources.insert (s.project);
    }
    auto fill = [] (juce::StringArray& a, const std::set<juce::String>& set) { a.clear(); for (auto& v : set) a.add (v); };
    fill (opts[(size_t) Category::Style], styles);
    fill (opts[(size_t) Category::Tags], tags);
    fill (opts[(size_t) Category::Source], sources);

    std::array<std::unordered_map<std::string, int>, (size_t) Category::Count> index;
    for (size_t c = 0; c < (size_t) Category::Count; ++c)
        for (int i = 0; i < opts[c].size(); ++i) index[c][opts[c][i].toStdString()] = i;

    member.assign (samples.size(), {});
    searchText.assign (samples.size(), {});
    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto& s = samples[i];
        for (size_t c = 0; c < (size_t) Category::Count; ++c)
            for (auto& o : memberships (s, (Category) c, hostBpm, compatibleKeys))
            {
                auto it = index[c].find (o.toStdString());
                if (it != index[c].end()) member[i][c].push_back (it->second);
            }
        searchText[i] = (s.path + " " + s.userTags.joinIntoString (" ") + " " + s.styles().joinIntoString (" ")
                         + " " + instrumentName (s.instrument()) + " " + s.keyDisplay()).toLowerCase();
    }
}

FilterModel::Result FilterModel::apply (const std::vector<SampleInfo>& samples, const FilterState& state) const
{
    Result r;
    constexpr size_t numCats = (size_t) Category::Count;
    std::array<std::vector<bool>, numCats> tickedIdx;
    std::array<bool, numCats> active {};
    for (size_t c = 0; c < numCats; ++c)
    {
        r.counts[c].assign ((size_t) opts[c].size(), 0);
        tickedIdx[c].assign ((size_t) opts[c].size(), false);
        for (auto& label : state.ticked[c])
        {
            int i = opts[c].indexOf (label);
            if (i >= 0) { tickedIdx[c][(size_t) i] = true; active[c] = true; }
        }
    }
    juce::StringArray words;
    words.addTokens (state.search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    const size_t n = juce::jmin (samples.size(), member.size());
    for (size_t i = 0; i < n; ++i)
    {
        if (state.favouritesOnly && ! samples[i].favourite) continue;
        bool textOk = true;
        for (auto& w : words) if (! searchText[i].contains (w)) { textOk = false; break; }
        if (! textOk) continue;

        int failed = 0; size_t failedCat = 0;
        for (size_t c = 0; c < numCats && failed < 2; ++c)
        {
            if (! active[c]) continue;
            bool ok = false;
            for (int o : member[i][c]) if (tickedIdx[c][(size_t) o]) { ok = true; break; }
            if (! ok) { ++failed; failedCat = c; }
        }
        if (failed == 0)
        {
            r.indices.push_back ((int) i);
            for (size_t c = 0; c < numCats; ++c)
                for (int o : member[i][c]) r.counts[c][(size_t) o]++;
        }
        else if (failed == 1)
        {
            // would match if this category's ticks were different: count it there
            for (int o : member[i][failedCat]) r.counts[failedCat][(size_t) o]++;
        }
    }
    return r;
}

juce::var FilterState::toVar() const
{
    auto* o = new juce::DynamicObject();
    for (size_t c = 0; c < (size_t) Category::Count; ++c)
    {
        juce::Array<juce::var> a;
        for (auto& s : ticked[c]) a.add (s);
        o->setProperty ("c" + juce::String ((int) c), a);
    }
    o->setProperty ("fav", favouritesOnly);
    o->setProperty ("compat", compatibleKeys);
    o->setProperty ("search", search);
    return juce::var (o);
}

void FilterState::fromVar (const juce::var& v)
{
    for (size_t c = 0; c < (size_t) Category::Count; ++c)
    {
        ticked[c].clear();
        if (auto* a = v.getProperty ("c" + juce::String ((int) c), {}).getArray())
            for (auto& s : *a) ticked[c].insert (s.toString());
    }
    favouritesOnly = v.getProperty ("fav", false);
    compatibleKeys = v.getProperty ("compat", false);
    search = v.getProperty ("search", "").toString();
}
}
