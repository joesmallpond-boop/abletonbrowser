#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace sb
{
// Key modes. A "Note" key means the sound is a single pitch (e.g. a tuned kick):
// it has a root note but no major/minor quality.
enum KeyMode { Major = 0, Minor = 1, NoteOnly = 2 };

namespace theory
{
    juce::String pitchClassName (int pc);               // 0 = C ... 11 = B (flats for black keys except F#, C#)
    juce::String keyName (int root, int mode, bool shortForm = true); // "G min" / "G minor" / "G"
    juce::String midiNoteName (float midi);             // "C3" (Ableton octave numbering, C3 = 60)

    // Krumhansl-Kessler key profiles
    const std::array<float, 12>& majorProfile();
    const std::array<float, 12>& minorProfile();

    bool scaleContains (int root, int mode, int pc);    // natural minor / major
    int relativeKeyRoot (int root, int mode);           // relative minor/major root

    struct KeyEstimate { int root = -1; int mode = Major; float score = 0; float margin = 0; };
    // Correlate a 12-bin chroma vector with all 24 key profiles.
    KeyEstimate estimateKey (const std::array<float, 12>& chroma, std::array<float, 24>* allScores = nullptr);

    inline int keyIndex (int root, int mode) { return mode * 12 + root; }  // 0-23
}
}
