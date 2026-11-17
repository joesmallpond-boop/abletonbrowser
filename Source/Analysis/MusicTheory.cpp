#include "MusicTheory.h"
#include <cmath>

namespace sb::theory
{
static const char* pcNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

juce::String pitchClassName (int pc)
{
    return pcNames[((pc % 12) + 12) % 12];
}

juce::String keyName (int root, int mode, bool shortForm)
{
    if (root < 0)
        return "-";
    auto n = pitchClassName (root);
    if (mode == NoteOnly) return n;
    if (shortForm) return n + (mode == Minor ? " min" : " maj");
    return n + (mode == Minor ? " minor" : " major");
}

juce::String midiNoteName (float midi)
{
    if (midi < 0) return "-";
    int n = (int) std::round (midi);
    int octave = n / 12 - 2;   // Ableton: C3 = 60
    return pitchClassName (n % 12) + juce::String (octave);
}

const std::array<float, 12>& majorProfile()
{
    static const std::array<float, 12> p { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    return p;
}

const std::array<float, 12>& minorProfile()
{
    static const std::array<float, 12> p { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    return p;
}

bool scaleContains (int root, int mode, int pc)
{
    static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* steps = mode == Minor ? minorSteps : majorSteps;
    int rel = ((pc - root) % 12 + 12) % 12;
    for (int i = 0; i < 7; ++i)
        if (steps[i] == rel) return true;
    return false;
}

int relativeKeyRoot (int root, int mode)
{
    return mode == Minor ? (root + 3) % 12 : (root + 9) % 12;
}

static float correlate (const std::array<float, 12>& a, const std::array<float, 12>& profile, int shift)
{
    float ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += profile[i]; }
    ma /= 12; mb /= 12;
    float num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i)
    {
        float x = a[(i + shift) % 12] - ma;
        float y = profile[i] - mb;
        num += x * y; da += x * x; db += y * y;
    }
    if (da <= 0 || db <= 0) return 0;
    return num / std::sqrt (da * db);
}

KeyEstimate estimateKey (const std::array<float, 12>& chroma, std::array<float, 24>* allScores)
{
    KeyEstimate best;
    float second = -2;
    best.score = -2;
    for (int mode = 0; mode < 2; ++mode)
        for (int root = 0; root < 12; ++root)
        {
            float r = correlate (chroma, mode == 0 ? majorProfile() : minorProfile(), root);
            if (allScores) (*allScores)[(size_t) keyIndex (root, mode)] = r;
            if (r > best.score)
            {
                second = best.score;
                best.score = r; best.root = root; best.mode = mode;
            }
            else if (r > second) second = r;
        }
    best.margin = best.score - second;
    return best;
}
}
