#pragma once
#include "Library/SampleInfo.h"

namespace sb::names
{
// Lower-case word tokens: "BigKick_01-Gm" -> big, kick, 01, gm
juce::StringArray tokenise (const juce::String& text);

struct InstrumentGuess { Instrument instrument = Instrument::Unknown; int strength = 0; };
// Looks for instrument words in a file name (strength 3) and its folders (strength 2 near, 1 far).
InstrumentGuess instrumentFromWords (const juce::StringArray& fileTokens, const juce::Array<juce::StringArray>& folderTokens);

struct KeyGuess { int root = -1; int mode = Major; bool found = false; };
// Finds key names like "Gm", "G minor", "F#min", "Bbmaj", "C# major", "in D".
KeyGuess keyFromText (const juce::String& text);

// "120bpm", "120 BPM", "bpm120" -> 120. Returns 0 if none.
double explicitBpmFromText (const juce::String& text);
// Bare numbers in a plausible tempo range ("Loop_124_..."), used only as a hint.
juce::Array<double> bpmCandidatesFromText (const juce::String& text);

// Genre / style words found in text ("dub", "techno", "lofi", ...), title-cased.
juce::StringArray stylesFromText (const juce::String& text);

bool mentionsLoop (const juce::StringArray& tokens);
bool mentionsOneShot (const juce::StringArray& tokens);
}
