#pragma once
#include <juce_core/juce_core.h>
#include "Analysis/MusicTheory.h"

namespace sb
{
enum class Instrument : int
{
    Unknown = 0, Kick, Snare, Clap, HiHat, Cymbal, Tom, Percussion, DrumLoop,
    Bass, Keys, Guitar, Synth, Pad, Lead, Pluck, Strings, Brass, Vocal, FX, Texture,
    Count
};
juce::String instrumentName (Instrument i);
bool isDrum (Instrument i);

enum class Tonality : int { Atonal = 0, SingleNote, Melody, Chords, Count };
juce::String tonalityName (Tonality t);

enum class SoundType : int { OneShot = 0, Loop, Count };

// Character tags, stored as bit flags.
enum CharacterFlag : juce::uint32
{
    chDark = 1u << 0, chBright = 1u << 1, chWarm = 1u << 2, chAiry = 1u << 3,
    chSaturated = 1u << 4, chPunchy = 1u << 5, chSoft = 1u << 6, chLoFi = 1u << 7,
    chClean = 1u << 8, chWide = 1u << 9, chMono = 1u << 10, chSustained = 1u << 11,
    chShortDecay = 1u << 12, chNoisy = 1u << 13,
    chNumFlags = 14
};
juce::String characterName (int bitIndex);

// Everything known about one audio file. "auto*" fields come from analysis and are
// overwritten on re-analysis. "user*" fields are manual corrections and survive rescans.
struct SampleInfo
{
    // file identity
    juce::String path;
    juce::int64 fileSize = 0;
    juce::int64 modTime = 0;

    // format
    double sampleRate = 44100;
    int numChannels = 1;
    double lengthSec = 0;

    // analysis
    double onsetSec = 0;          // where the sound actually starts (leading silence skipped)
    double tailSec = 0;           // where it decays below -60 dB
    float peakDb = -100, rmsDb = -100;
    float attackMs = 0, decaySec = 0, centroidHz = 0, flatness = 0, stereoWidth = 0;

    Instrument autoInstrument = Instrument::Unknown;
    float instrumentConfidence = 0;
    bool instrumentFromName = false;

    int autoKeyRoot = -1;         // 0-11, -1 = no key
    int autoKeyMode = Major;
    float keyConfidence = 0;

    float autoRootMidi = -1;      // detected pitch for sampler mapping (fractional = cents)
    float pitchConfidence = 0;
    float medianPitchMidi = -1;   // used for register

    Tonality tonality = Tonality::Atonal;
    SoundType autoType = SoundType::OneShot;
    double autoBpm = 0;           // 0 = no tempo
    double projectBpm = 0;        // tempo of the Ableton project the file came from
    juce::uint32 character = 0;
    juce::StringArray autoStyles; // from file/folder/project names
    juce::String project;         // Ableton project name, or library folder name
    juce::String root;            // the scan location it was found under

    // user overrides
    bool favourite = false;
    int userInstrument = -1;
    int userKeyRoot = -2;         // -2 = not set, -1 = forced "no key"
    int userKeyMode = Major;
    double userBpm = -1;          // -1 = not set, 0 = forced "no tempo"
    float userRootMidi = -1000;   // -1000 = not set
    int userType = -1;
    juce::StringArray userStyles;
    juce::StringArray userTags;

    // effective values
    Instrument instrument() const { return userInstrument >= 0 ? (Instrument) userInstrument : autoInstrument; }
    int keyRoot() const { return userKeyRoot != -2 ? userKeyRoot : autoKeyRoot; }
    int keyMode() const { return userKeyRoot != -2 ? userKeyMode : autoKeyMode; }
    double bpm() const { return userBpm >= 0 ? userBpm : autoBpm; }
    float rootMidi() const { return userRootMidi > -999 ? userRootMidi : autoRootMidi; }
    SoundType type() const { return userType >= 0 ? (SoundType) userType : autoType; }
    juce::StringArray styles() const;
    double effectiveLength() const { return juce::jmax (0.0, (tailSec > onsetSec ? tailSec : lengthSec) - onsetSec); }
    juce::String fileName() const { return juce::File (path).getFileNameWithoutExtension(); }
    juce::String keyDisplay() const { return theory::keyName (keyRoot(), keyMode()); }

    void copyUserFieldsFrom (const SampleInfo& other);
    bool hasUserEdits() const;
    void clearUserEdits();

    void write (juce::OutputStream& out) const;
    bool read (juce::InputStream& in, int version);
    static constexpr int serialVersion = 1;
};
}
