#include "SampleInfo.h"

namespace sb
{
juce::String instrumentName (Instrument i)
{
    switch (i)
    {
        case Instrument::Unknown:    return "Unsorted";
        case Instrument::Kick:       return "Kick";
        case Instrument::Snare:      return "Snare";
        case Instrument::Clap:       return "Clap";
        case Instrument::HiHat:      return "Hi-hat";
        case Instrument::Cymbal:     return "Cymbal";
        case Instrument::Tom:        return "Tom";
        case Instrument::Percussion: return "Percussion";
        case Instrument::DrumLoop:   return "Drum loop";
        case Instrument::Bass:       return "Bass";
        case Instrument::Keys:       return "Keys";
        case Instrument::Guitar:     return "Guitar";
        case Instrument::Synth:      return "Synth";
        case Instrument::Pad:        return "Pad";
        case Instrument::Lead:       return "Lead";
        case Instrument::Pluck:      return "Pluck";
        case Instrument::Strings:    return "Strings";
        case Instrument::Brass:      return "Brass & wind";
        case Instrument::Vocal:      return "Vocal";
        case Instrument::FX:         return "FX";
        case Instrument::Texture:    return "Texture";
        default: break;
    }
    return "?";
}

bool isDrum (Instrument i)
{
    return i == Instrument::Kick || i == Instrument::Snare || i == Instrument::Clap || i == Instrument::HiHat
        || i == Instrument::Cymbal || i == Instrument::Tom || i == Instrument::Percussion || i == Instrument::DrumLoop;
}

juce::String tonalityName (Tonality t)
{
    switch (t)
    {
        case Tonality::Atonal:     return "No clear pitch";
        case Tonality::SingleNote: return "Single note";
        case Tonality::Melody:     return "Melody";
        case Tonality::Chords:     return "Chords";
        default: break;
    }
    return "?";
}

juce::String characterName (int bit)
{
    static const char* names[chNumFlags] = { "Dark", "Bright", "Warm", "Airy", "Saturated", "Punchy", "Soft",
                                             "Lo-fi", "Clean", "Wide", "Mono", "Sustained", "Short decay", "Noisy" };
    return juce::isPositiveAndBelow (bit, (int) chNumFlags) ? names[bit] : "?";
}

juce::StringArray SampleInfo::styles() const
{
    juce::StringArray s (autoStyles);
    s.addArray (userStyles);
    s.removeDuplicates (true);
    s.removeEmptyStrings();
    return s;
}

void SampleInfo::copyUserFieldsFrom (const SampleInfo& o)
{
    favourite = o.favourite; userInstrument = o.userInstrument; userKeyRoot = o.userKeyRoot;
    userKeyMode = o.userKeyMode; userBpm = o.userBpm; userRootMidi = o.userRootMidi;
    userType = o.userType; userStyles = o.userStyles; userTags = o.userTags;
}

bool SampleInfo::hasUserEdits() const
{
    return userInstrument >= 0 || userKeyRoot != -2 || userBpm >= 0 || userRootMidi > -999
        || userType >= 0 || ! userStyles.isEmpty() || ! userTags.isEmpty();
}

void SampleInfo::clearUserEdits()
{
    userInstrument = -1; userKeyRoot = -2; userKeyMode = Major; userBpm = -1;
    userRootMidi = -1000; userType = -1; userStyles.clear(); userTags.clear();
}

static void writeStrings (juce::OutputStream& out, const juce::StringArray& a)
{
    out.writeCompressedInt (a.size());
    for (auto& s : a) out.writeString (s);
}

static juce::StringArray readStrings (juce::InputStream& in)
{
    juce::StringArray a;
    int n = in.readCompressedInt();
    for (int i = 0; i < n && ! in.isExhausted(); ++i) a.add (in.readString());
    return a;
}

void SampleInfo::write (juce::OutputStream& out) const
{
    out.writeString (path);
    out.writeInt64 (fileSize); out.writeInt64 (modTime);
    out.writeDouble (sampleRate); out.writeInt (numChannels); out.writeDouble (lengthSec);
    out.writeDouble (onsetSec); out.writeDouble (tailSec);
    out.writeFloat (peakDb); out.writeFloat (rmsDb); out.writeFloat (attackMs); out.writeFloat (decaySec);
    out.writeFloat (centroidHz); out.writeFloat (flatness); out.writeFloat (stereoWidth);
    out.writeInt ((int) autoInstrument); out.writeFloat (instrumentConfidence); out.writeBool (instrumentFromName);
    out.writeInt (autoKeyRoot); out.writeInt (autoKeyMode); out.writeFloat (keyConfidence);
    out.writeFloat (autoRootMidi); out.writeFloat (pitchConfidence); out.writeFloat (medianPitchMidi);
    out.writeInt ((int) tonality); out.writeInt ((int) autoType);
    out.writeDouble (autoBpm); out.writeDouble (projectBpm);
    out.writeInt ((int) character);
    writeStrings (out, autoStyles);
    out.writeString (project); out.writeString (root);
    out.writeBool (favourite); out.writeInt (userInstrument); out.writeInt (userKeyRoot); out.writeInt (userKeyMode);
    out.writeDouble (userBpm); out.writeFloat (userRootMidi); out.writeInt (userType);
    writeStrings (out, userStyles); writeStrings (out, userTags);
}

bool SampleInfo::read (juce::InputStream& in, int version)
{
    juce::ignoreUnused (version);
    path = in.readString();
    fileSize = in.readInt64(); modTime = in.readInt64();
    sampleRate = in.readDouble(); numChannels = in.readInt(); lengthSec = in.readDouble();
    onsetSec = in.readDouble(); tailSec = in.readDouble();
    peakDb = in.readFloat(); rmsDb = in.readFloat(); attackMs = in.readFloat(); decaySec = in.readFloat();
    centroidHz = in.readFloat(); flatness = in.readFloat(); stereoWidth = in.readFloat();
    autoInstrument = (Instrument) juce::jlimit (0, (int) Instrument::Count - 1, in.readInt());
    instrumentConfidence = in.readFloat(); instrumentFromName = in.readBool();
    autoKeyRoot = in.readInt(); autoKeyMode = in.readInt(); keyConfidence = in.readFloat();
    autoRootMidi = in.readFloat(); pitchConfidence = in.readFloat(); medianPitchMidi = in.readFloat();
    tonality = (Tonality) juce::jlimit (0, (int) Tonality::Count - 1, in.readInt());
    autoType = (SoundType) juce::jlimit (0, 1, in.readInt());
    autoBpm = in.readDouble(); projectBpm = in.readDouble();
    character = (juce::uint32) in.readInt();
    autoStyles = readStrings (in);
    project = in.readString(); root = in.readString();
    favourite = in.readBool(); userInstrument = in.readInt(); userKeyRoot = in.readInt(); userKeyMode = in.readInt();
    userBpm = in.readDouble(); userRootMidi = in.readFloat(); userType = in.readInt();
    userStyles = readStrings (in); userTags = readStrings (in);
    return path.isNotEmpty();
}
}
