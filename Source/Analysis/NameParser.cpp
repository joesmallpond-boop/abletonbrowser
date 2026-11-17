#include "NameParser.h"
#include <regex>
#include <map>

namespace sb::names
{
juce::StringArray tokenise (const juce::String& text)
{
    juce::StringArray out;
    juce::String cur;
    auto flush = [&] { if (cur.isNotEmpty()) out.add (cur.toLowerCase()); cur.clear(); };

    juce::juce_wchar prev = 0;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        auto c = *p;
        bool alnum = juce::CharacterFunctions::isLetterOrDigit (c);
        if (! alnum) { flush(); prev = 0; continue; }
        if (prev != 0)
        {
            bool split = (juce::CharacterFunctions::isLowerCase (prev) && juce::CharacterFunctions::isUpperCase (c))
                      || (juce::CharacterFunctions::isDigit (prev) != juce::CharacterFunctions::isDigit (c));
            if (split) flush();
        }
        cur += c;
        prev = c;
    }
    flush();
    return out;
}

static const std::map<juce::String, Instrument>& keywordMap()
{
    static const std::map<juce::String, Instrument> m = [] {
        std::map<juce::String, Instrument> k;
        auto add = [&] (std::initializer_list<const char*> words, Instrument i) { for (auto w : words) k[w] = i; };
        add ({ "kick", "kik", "kck", "bd", "bassdrum", "kicks" }, Instrument::Kick);
        add ({ "snare", "snr", "sd", "rim", "rimshot", "sidestick", "snares" }, Instrument::Snare);
        add ({ "clap", "clp", "claps", "snap", "snaps", "fingersnap" }, Instrument::Clap);
        add ({ "hat", "hats", "hihat", "hh", "hihats", "openhat", "closedhat", "ohh", "chh" }, Instrument::HiHat);
        add ({ "cymbal", "cym", "crash", "ride", "splash", "china", "cymbals" }, Instrument::Cymbal);
        add ({ "tom", "toms", "floortom" }, Instrument::Tom);
        add ({ "perc", "percussion", "shaker", "shakers", "conga", "congas", "bongo", "bongos", "tamb", "tambourine",
               "cowbell", "clave", "claves", "woodblock", "timbale", "guiro", "cabasa", "triangle", "agogo" }, Instrument::Percussion);
        add ({ "drumloop", "drums", "drum", "break", "breaks", "breakbeat", "toploop", "beat", "beats", "groove" }, Instrument::DrumLoop);
        add ({ "bass", "sub", "808", "subbass", "bassline", "reese", "basses" }, Instrument::Bass);
        add ({ "keys", "piano", "rhodes", "wurli", "wurlitzer", "organ", "epiano", "ep", "clav", "clavinet", "harpsichord", "celeste", "mallet", "marimba", "vibes", "vibraphone", "bell", "bells" }, Instrument::Keys);
        add ({ "guitar", "gtr", "gtrs", "guitars", "strum", "riff" }, Instrument::Guitar);
        add ({ "synth", "synths", "arp", "arps", "seq", "stab", "stabs", "chord", "chords" }, Instrument::Synth);
        add ({ "pad", "pads" }, Instrument::Pad);
        add ({ "lead", "leads" }, Instrument::Lead);
        add ({ "pluck", "plucks", "pizz" }, Instrument::Pluck);
        add ({ "string", "strings", "violin", "viola", "cello", "orchestra", "orch", "harp" }, Instrument::Strings);
        add ({ "brass", "horn", "horns", "trumpet", "trombone", "sax", "saxophone", "flute", "clarinet", "wind", "woodwind" }, Instrument::Brass);
        add ({ "vox", "vocal", "vocals", "voice", "acapella", "vocalchop", "chop", "choir", "adlib", "spoken" }, Instrument::Vocal);
        add ({ "fx", "sfx", "riser", "risers", "sweep", "impact", "whoosh", "downlifter", "uplifter", "noise", "glitch", "zap", "laser", "reverse", "rev" }, Instrument::FX);
        add ({ "texture", "textures", "ambience", "ambient", "atmos", "atmosphere", "drone", "field", "foley", "soundscape" }, Instrument::Texture);
        return k;
    }();
    return m;
}

static Instrument lookupWord (const juce::String& tok)
{
    auto& m = keywordMap();
    auto it = m.find (tok);
    if (it != m.end()) return it->second;
    if (tok.length() > 4 && tok.endsWithChar ('s'))
    {
        it = m.find (tok.dropLastCharacters (1));
        if (it != m.end()) return it->second;
    }
    return Instrument::Unknown;
}

static InstrumentGuess fromTokens (const juce::StringArray& tokens, int strength)
{
    // Combined phrases first
    for (int i = 0; i + 1 < tokens.size(); ++i)
    {
        auto pair = tokens[i] + tokens[i + 1];
        if (pair == "drumloop" || pair == "toploop" || pair == "breakbeat") return { Instrument::DrumLoop, strength };
        if (pair == "openhat" || pair == "closedhat" || pair == "hihat") return { Instrument::HiHat, strength };
        if (pair == "bassdrum") return { Instrument::Kick, strength };
        if (pair == "subbass") return { Instrument::Bass, strength };
        if (pair == "vocalchop") return { Instrument::Vocal, strength };
    }
    // Prefer the most specific word: drum-part words beat generic "drums"/"loop" words.
    Instrument found = Instrument::Unknown;
    for (auto& t : tokens)
    {
        auto i = lookupWord (t);
        if (i == Instrument::Unknown) continue;
        if (found == Instrument::Unknown || (found == Instrument::DrumLoop && i != Instrument::DrumLoop)
            || (found == Instrument::Synth && i != Instrument::Synth))
            found = i;
    }
    return { found, found == Instrument::Unknown ? 0 : strength };
}

InstrumentGuess instrumentFromWords (const juce::StringArray& fileTokens, const juce::Array<juce::StringArray>& folderTokens)
{
    auto g = fromTokens (fileTokens, 3);
    if (g.instrument != Instrument::Unknown) return g;
    for (int i = 0; i < folderTokens.size(); ++i)
    {
        g = fromTokens (folderTokens.getReference (i), i == 0 ? 2 : 1);
        if (g.instrument != Instrument::Unknown) return g;
    }
    return {};
}

static int letterToPc (char c)
{
    switch (c) { case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
                 case 'G': return 7; case 'A': return 9; case 'B': return 11; default: return -1; }
}

KeyGuess keyFromText (const juce::String& textIn)
{
    auto text = textIn.replace (juce::CharPointer_UTF8 ("\xe2\x99\xaf"), "#")
                      .replace (juce::CharPointer_UTF8 ("\xe2\x99\xad"), "b")
                      .toStdString();

    static const std::regex re (R"((?:^|[^A-Za-z])([A-G])(#|b|sharp|flat|s(?=[ _-]?(?:min|maj|m)))?[ _-]?(minor|min|major|maj|mi|m|M)?(?=[^A-Za-z]|$))");
    KeyGuess best;
    for (auto it = std::sregex_iterator (text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
    {
        auto& m = *it;
        std::string acc = m[2].str(), qual = m[3].str();
        std::string whole = m[0].str();
        // Skip "FM"/"AM" (synthesis / radio) and lone letters without accidental or quality
        if ((qual == "M") && (m[1].str() == "F" || m[1].str() == "A")) continue;
        bool lone = acc.empty() && qual.empty();
        if (lone)
        {
            // accept "key of G", "in G", "key G"
            auto pos = (size_t) m.position (0);
            auto before = juce::String (text.substr (0, pos)).toLowerCase().trimEnd();
            if (! (before.endsWith ("key") || before.endsWith ("key of") || before.endsWith (" in") || before == "in"))
                continue;
        }
        int pc = letterToPc (m[1].str()[0]);
        if (acc == "#" || acc == "sharp" || acc == "s") pc += 1;
        if (acc == "b" || acc == "flat") pc -= 1;
        pc = (pc + 12) % 12;
        int mode = Major;
        if (qual == "m" || qual == "min" || qual == "minor" || qual == "mi") mode = Minor;
        best.root = pc; best.mode = mode; best.found = true;
        if (! lone) return best; // explicit form wins immediately
    }
    return best;
}

double explicitBpmFromText (const juce::String& textIn)
{
    auto text = textIn.toLowerCase().toStdString();
    static const std::regex a (R"((\d{2,3}(?:\.\d+)?)\s*[_-]?\s*bpm)");
    static const std::regex b (R"(bpm\s*[_-]?\s*(\d{2,3}(?:\.\d+)?))");
    std::smatch m;
    for (auto* r : { &a, &b })
        if (std::regex_search (text, m, *r))
        {
            double v = std::stod (m[1].str());
            if (v >= 40 && v <= 300) return v;
        }
    return 0;
}

juce::Array<double> bpmCandidatesFromText (const juce::String& text)
{
    juce::Array<double> out;
    for (auto& t : tokenise (text))
        if (t.containsOnly ("0123456789") && t.length() >= 2 && t.length() <= 3)
        {
            int v = t.getIntValue();
            if (v >= 60 && v <= 200 && v != 808 && v != 909 && v != 101 && v != 303 && v != 606 && v != 707)
                out.add (v);
        }
    return out;
}

juce::StringArray stylesFromText (const juce::String& text)
{
    static const std::map<juce::String, juce::String> styles = [] {
        std::map<juce::String, juce::String> s;
        auto add = [&] (std::initializer_list<const char*> words, const char* style) { for (auto w : words) s[w] = style; };
        add ({ "dub", "dubby" }, "Dub");
        add ({ "techno" }, "Techno");
        add ({ "house", "deephouse" }, "House");
        add ({ "minimal" }, "Minimal");
        add ({ "ambient" }, "Ambient");
        add ({ "trap" }, "Trap");
        add ({ "hiphop", "boombap", "rap" }, "Hip hop");
        add ({ "lofi" }, "Lo-fi");
        add ({ "dnb", "drumandbass", "neuro" }, "Drum & bass");
        add ({ "jungle" }, "Jungle");
        add ({ "garage", "ukg", "2step" }, "Garage");
        add ({ "dubstep" }, "Dubstep");
        add ({ "grime" }, "Grime");
        add ({ "drill" }, "Drill");
        add ({ "disco" }, "Disco");
        add ({ "funk", "funky" }, "Funk");
        add ({ "soul", "soulful" }, "Soul");
        add ({ "jazz", "jazzy" }, "Jazz");
        add ({ "rnb" }, "R&B");
        add ({ "rock" }, "Rock");
        add ({ "punk" }, "Punk");
        add ({ "metal" }, "Metal");
        add ({ "doom", "sludge", "stoner" }, "Doom");
        add ({ "indie" }, "Indie");
        add ({ "pop" }, "Pop");
        add ({ "cinematic", "film", "score" }, "Cinematic");
        add ({ "experimental", "weird" }, "Experimental");
        add ({ "industrial" }, "Industrial");
        add ({ "acid" }, "Acid");
        add ({ "breaks", "breakbeat" }, "Breaks");
        add ({ "electro" }, "Electro");
        add ({ "trance" }, "Trance");
        add ({ "idm" }, "IDM");
        add ({ "glitch", "glitchy" }, "Glitch");
        add ({ "downtempo", "chill", "chillout" }, "Downtempo");
        add ({ "reggae", "dancehall", "roots" }, "Reggae");
        add ({ "afro", "afrobeat", "amapiano" }, "Afro");
        add ({ "latin", "salsa", "reggaeton" }, "Latin");
        add ({ "gospel" }, "Gospel");
        add ({ "folk" }, "Folk");
        add ({ "blues", "bluesy" }, "Blues");
        add ({ "synthwave", "retrowave" }, "Synthwave");
        add ({ "shoegaze" }, "Shoegaze");
        add ({ "drone" }, "Drone");
        add ({ "psych", "psychedelic" }, "Psych");
        add ({ "orchestral" }, "Orchestral");
        add ({ "vintage", "retro" }, "Vintage");
        add ({ "tape", "cassette" }, "Tape");
        add ({ "vinyl", "dusty" }, "Vinyl");
        add ({ "dark" }, "Dark");
        add ({ "dirty", "gritty", "grimy" }, "Dirty");
        add ({ "dreamy", "dream" }, "Dreamy");
        add ({ "wonky" }, "Wonky");
        add ({ "heavy" }, "Heavy");
        return s;
    }();

    juce::StringArray out;
    auto tokens = tokenise (text);
    for (int i = 0; i < tokens.size(); ++i)
    {
        auto it = styles.find (tokens[i]);
        if (it != styles.end()) out.addIfNotAlreadyThere (it->second);
        if (i + 1 < tokens.size())
        {
            auto pair = styles.find (tokens[i] + tokens[i + 1]);
            if (pair != styles.end()) out.addIfNotAlreadyThere (pair->second);
        }
    }
    return out;
}

bool mentionsLoop (const juce::StringArray& tokens)
{
    for (auto& t : tokens)
        if (t == "loop" || t == "loops" || t == "lp" || t == "groove" || t == "break" || t == "breaks" || t == "phrase")
            return true;
    return false;
}

bool mentionsOneShot (const juce::StringArray& tokens)
{
    for (int i = 0; i < tokens.size(); ++i)
    {
        if (tokens[i] == "oneshot" || tokens[i] == "hit" || tokens[i] == "os") return true;
        if (tokens[i] == "one" && i + 1 < tokens.size() && tokens[i + 1] == "shot") return true;
    }
    return false;
}
}
