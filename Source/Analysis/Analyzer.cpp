#include "Analyzer.h"
#include "NameParser.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <numeric>

namespace sb
{
namespace
{
constexpr double kAnalysisRate = 22050.0;
constexpr float kPi = 3.14159265358979f;

inline float toDb (float g) { return g > 1e-10f ? 20.0f * std::log10 (g) : -200.0f; }
inline float freqToMidi (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }

float median (std::vector<float> v)
{
    if (v.empty()) return 0;
    auto mid = v.begin() + (std::ptrdiff_t) (v.size() / 2);
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

std::vector<float> resampleMono (const juce::AudioBuffer<float>& in, double sr, double outRate, int maxInSamples)
{
    int n = juce::jmin (in.getNumSamples(), maxInSamples);
    std::vector<float> mono ((size_t) n, 0.0f);
    int chans = in.getNumChannels();
    for (int c = 0; c < chans; ++c)
    {
        auto* d = in.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i] / (float) chans;
    }
    if (std::abs (sr - outRate) < 1.0) return mono;

    double ratio = sr / outRate;
    // light low-pass before decimation (simple 2-pole) to limit aliasing
    if (ratio > 1.0)
    {
        juce::IIRFilter lp;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, outRate * 0.45));
        lp.processSamples (mono.data(), n);
    }
    int outN = (int) std::floor (n / ratio);
    std::vector<float> out ((size_t) juce::jmax (0, outN));
    juce::LagrangeInterpolator interp;
    interp.process (ratio, mono.data(), out.data(), outN, n, 0);
    return out;
}

struct Spectrum
{
    explicit Spectrum (int order) : fft (order), size (1 << order), window ((size_t) size), buf ((size_t) size * 2)
    {
        for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) size);
    }
    // magnitudes for bins 0..size/2 of x[start .. start+size)
    const float* compute (const std::vector<float>& x, int start)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i)
        {
            int j = start + i;
            buf[(size_t) i] = (j >= 0 && j < (int) x.size()) ? x[(size_t) j] * window[(size_t) i] : 0.0f;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        return buf.data();
    }
    juce::dsp::FFT fft;
    int size;
    std::vector<float> window, buf;
};

// YIN pitch detector using FFT cross-correlation
struct Yin
{
    static constexpr int W = 1024;
    static constexpr int frame = 2048;
    Yin() : fft (12), a (4096), b (4096), prod (4096) {}

    // returns f0 (Hz) or 0 if unvoiced; aperiodicity in ap
    float detect (const std::vector<float>& x, int start, double sr, float& ap)
    {
        int tauMin = (int) (sr / 1500.0), tauMax = juce::jmin (W - 1, (int) (sr / 32.0));
        std::fill (a.begin(), a.end(), std::complex<float>());
        std::fill (b.begin(), b.end(), std::complex<float>());
        for (int i = 0; i < frame; ++i)
        {
            int j = start + i;
            float v = (j >= 0 && j < (int) x.size()) ? x[(size_t) j] : 0.0f;
            b[(size_t) i] = v;
            if (i < W) a[(size_t) i] = v;
        }
        std::vector<std::complex<float>> A (4096), B (4096);
        fft.perform (a.data(), A.data(), false);
        fft.perform (b.data(), B.data(), false);
        for (size_t i = 0; i < 4096; ++i) prod[i] = std::conj (A[i]) * B[i];
        std::vector<std::complex<float>> r (4096);
        fft.perform (prod.data(), r.data(), true); // juce inverse is scaled by 1/N already

        // energy prefix sums
        std::vector<double> pre ((size_t) frame + 1, 0.0);
        for (int i = 0; i < frame; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + (double) b[(size_t) i].real() * b[(size_t) i].real();
        double e0 = pre[W];
        if (e0 < 1e-9) { ap = 1; return 0; }

        std::vector<float> d ((size_t) tauMax + 2, 1.0f);
        double running = 0;
        d[0] = 1;
        int found = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double et = pre[(size_t) (tau + W)] - pre[(size_t) tau];
            double diff = e0 + et - 2.0 * r[(size_t) tau].real();
            running += diff;
            d[(size_t) tau] = running > 0 ? (float) (diff * tau / running) : 1.0f;
        }
        const float thresh = 0.15f;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < thresh)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                found = tau; break;
            }
        if (found < 0)
        {
            int best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (d[(size_t) tau] < d[(size_t) best]) best = tau;
            ap = d[(size_t) best];
            if (ap > 0.3f) return 0;
            found = best;
        }
        ap = d[(size_t) found];
        float t = (float) found;
        if (found > 1 && found + 1 < tauMax)
        {
            float s0 = d[(size_t) found - 1], s1 = d[(size_t) found], s2 = d[(size_t) found + 1];
            float denom = s0 + s2 - 2 * s1;
            if (std::abs (denom) > 1e-9f) t += 0.5f * (s0 - s2) / denom;
        }
        return (float) (sr / t);
    }

    juce::dsp::FFT fft;
    std::vector<std::complex<float>> a, b, prod;
};
} // namespace

Analyzer::Analyzer()
{
    formats.registerBasicFormats();
}

juce::String Analyzer::fileWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.mp3;*.ogg;*.m4a;*.caf";
}

bool Analyzer::isAudioFile (const juce::File& f)
{
    auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".flac"
        || ext == ".mp3" || ext == ".ogg" || ext == ".m4a" || ext == ".caf";
}

NameHints Analyzer::makeHints (const juce::File& file, const juce::File& scanRoot, const ProjectInfo& project)
{
    NameHints h;
    auto base = file.getFileNameWithoutExtension();
    h.fileTokens = names::tokenise (base);

    // folders from nearest up to the scan root (max 4 levels)
    auto dir = file.getParentDirectory();
    for (int i = 0; i < 4; ++i)
    {
        auto name = dir.getFileName();
        // Ableton's own folder names carry no meaning
        if (! (name == "Samples" || name == "Recorded" || name == "Processed" || name == "Imported" || name == "Consolidate"
               || name == "Crop" || name == "Reverse" || name == "Freeze" || name.endsWith (" Project")))
            h.folderTokens.add (names::tokenise (name));
        if (dir == scanRoot || dir.isRoot()) break;
        dir = dir.getParentDirectory();
    }

    auto fk = names::keyFromText (base);
    if (! fk.found)
        for (auto d = file.getParentDirectory(); d != scanRoot && ! d.isRoot() && d.isAChildOf (scanRoot); d = d.getParentDirectory())
        {
            if (project.isProject && d == project.folder) break;
            fk = names::keyFromText (d.getFileName());
            if (fk.found) break;
        }
    if (fk.found) { h.fileKeyRoot = fk.root; h.fileKeyMode = fk.mode; }

    if (project.isProject)
    {
        auto pk = names::keyFromText (project.name);
        if (! pk.found) pk = names::keyFromText (project.setNames);
        if (pk.found) { h.projectKeyRoot = pk.root; h.projectKeyMode = pk.mode; }
        h.projectBpm = project.tempo;
    }

    h.explicitBpm = names::explicitBpmFromText (base);
    if (h.explicitBpm <= 0) h.explicitBpm = names::explicitBpmFromText (file.getParentDirectory().getFileName());
    h.bpmCandidates = names::bpmCandidatesFromText (base);
    h.saysLoop = names::mentionsLoop (h.fileTokens);
    h.saysOneShot = names::mentionsOneShot (h.fileTokens);
    if (! h.saysLoop && ! h.saysOneShot && ! h.folderTokens.isEmpty())
    {
        h.saysLoop = names::mentionsLoop (h.folderTokens.getReference (0));
        h.saysOneShot = names::mentionsOneShot (h.folderTokens.getReference (0));
    }
    return h;
}

bool Analyzer::analyseFile (const juce::File& file, const juce::File& scanRoot, ProjectLocator& projects, SampleInfo& out)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0) return false;

    out.path = file.getFullPathName();
    out.fileSize = file.getSize();
    out.modTime = file.getLastModificationTime().toMilliseconds();
    out.sampleRate = reader->sampleRate;
    out.numChannels = (int) reader->numChannels;
    out.lengthSec = (double) reader->lengthInSamples / reader->sampleRate;
    out.root = scanRoot.getFullPathName();

    int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    auto toRead = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxAnalysisSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buf (chans, toRead);
    reader->read (&buf, 0, toRead, 0, true, chans > 1);

    auto project = projects.find (file, scanRoot);
    auto hints = makeHints (file, scanRoot, project);
    out.projectBpm = project.tempo;
    out.project = project.isProject ? project.name : scanRoot.getFileName();

    juce::String styleText = file.getFileNameWithoutExtension();
    for (auto d = file.getParentDirectory(); d.isAChildOf (scanRoot) || d == scanRoot; d = d.getParentDirectory())
    {
        styleText << " " << d.getFileName();
        if (d == scanRoot) break;
    }
    if (project.isProject) styleText << " " << project.name << " " << project.setNames;
    out.autoStyles = names::stylesFromText (styleText);

    analyseAudio (buf, reader->sampleRate, hints, out);
    return true;
}

void Analyzer::analyseAudio (const juce::AudioBuffer<float>& audio, double sr, const NameHints& hints,
                             SampleInfo& out, AnalysisDebug* debug)
{
    AnalysisDebug dbgLocal;
    auto& dbg = debug ? *debug : dbgLocal;
    const int n = audio.getNumSamples();
    if (out.lengthSec <= 0) out.lengthSec = n / sr;
    out.sampleRate = sr;
    if (out.numChannels <= 0) out.numChannels = audio.getNumChannels();
    if (n <= 0) return;

    // ---------- level, onset, tail on the original signal (1 ms blocks) ----------
    const int blk = juce::jmax (1, (int) (sr * 0.001));
    const int numBlk = (n + blk - 1) / blk;
    std::vector<float> blockPeak ((size_t) numBlk, 0.0f);
    double sumSq = 0, sideSq = 0, midSq = 0;
    for (int b = 0; b < numBlk; ++b)
    {
        float pk = 0;
        for (int i = b * blk; i < juce::jmin (n, (b + 1) * blk); ++i)
            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                float v = audio.getSample (c, i);
                pk = juce::jmax (pk, std::abs (v));
            }
        blockPeak[(size_t) b] = pk;
    }
    for (int i = 0; i < n; ++i)
    {
        float l = audio.getSample (0, i), r = audio.getNumChannels() > 1 ? audio.getSample (1, i) : l;
        float m = 0.5f * (l + r), s = 0.5f * (l - r);
        sumSq += (double) m * m; midSq += (double) m * m; sideSq += (double) s * s;
    }
    float peak = *std::max_element (blockPeak.begin(), blockPeak.end());
    out.peakDb = toDb (peak);
    out.stereoWidth = midSq > 1e-12 ? (float) std::sqrt (sideSq / midSq) : 0.0f;

    if (peak < 1e-5f) // silent file
    {
        out.onsetSec = 0; out.tailSec = out.lengthSec; out.rmsDb = -200;
        out.autoInstrument = Instrument::Unknown; out.tonality = Tonality::Atonal;
        return;
    }

    const float onThresh = juce::jmax (peak * 0.01f, 0.0003f);   // -40 dB below peak
    const float tailThresh = juce::jmax (peak * 0.001f, 0.00003f); // -60 dB below peak
    int onsetBlk = 0;
    while (onsetBlk < numBlk && blockPeak[(size_t) onsetBlk] < onThresh) ++onsetBlk;
    onsetBlk = juce::jmax (0, onsetBlk - 1);
    int tailBlk = numBlk - 1;
    while (tailBlk > onsetBlk && blockPeak[(size_t) tailBlk] < tailThresh) --tailBlk;
    out.onsetSec = juce::jmax (0.0, onsetBlk * blk / sr);
    out.tailSec = juce::jmin (out.lengthSec, (tailBlk + 1) * blk / sr);
    const double effLen = juce::jmax (0.001, out.tailSec - out.onsetSec);

    // smoothed envelopes (1 ms hop): 10 ms RMS for attack/decay, 4 ms RMS for clap detection
    auto rmsEnvelope = [&] (int windowBlocks) {
        std::vector<float> e ((size_t) numBlk, 0.0f);
        std::vector<double> blockEnergy ((size_t) numBlk, 0.0);
        for (int b = 0; b < numBlk; ++b)
        {
            double s = 0; int cnt = 0;
            for (int i = b * blk; i < juce::jmin (n, (b + 1) * blk); ++i, ++cnt)
                for (int c = 0; c < audio.getNumChannels(); ++c) { float v = audio.getSample (c, i); s += (double) v * v; }
            blockEnergy[(size_t) b] = cnt > 0 ? s / (cnt * audio.getNumChannels()) : 0.0;
        }
        double run = 0;
        for (int b = 0; b < numBlk; ++b)
        {
            run += blockEnergy[(size_t) b];
            if (b >= windowBlocks) run -= blockEnergy[(size_t) (b - windowBlocks)];
            e[(size_t) b] = (float) std::sqrt (juce::jmax (0.0, run) / juce::jmin (b + 1, windowBlocks));
        }
        return e;
    };
    auto env10 = rmsEnvelope (10);
    float envMax = 0; int envMaxBlk = onsetBlk;
    for (int b = onsetBlk; b <= tailBlk; ++b) if (env10[(size_t) b] > envMax) { envMax = env10[(size_t) b]; envMaxBlk = b; }
    int attackBlk = onsetBlk;
    while (attackBlk < envMaxBlk && env10[(size_t) attackBlk] < envMax * 0.7f) ++attackBlk;
    out.attackMs = (float) ((attackBlk - onsetBlk) * blk / sr * 1000.0);
    int decayBlk = envMaxBlk;
    while (decayBlk < numBlk && env10[(size_t) decayBlk] > envMax * 0.1f) ++decayBlk;
    out.decaySec = (float) ((decayBlk - envMaxBlk) * blk / sr);

    // clap: several distinct bursts in the first 60 ms
    {
        auto env4 = rmsEnvelope (4);
        float m4 = 0;
        for (int b = onsetBlk; b < juce::jmin (numBlk, onsetBlk + 80); ++b) m4 = juce::jmax (m4, env4[(size_t) b]);
        int count = 0; float localPeak = 0; bool rising = true;
        for (int b = onsetBlk; b < juce::jmin (numBlk, onsetBlk + 60); ++b)
        {
            float v = env4[(size_t) b];
            if (rising)
            {
                if (v > localPeak) localPeak = v;
                else if (localPeak > m4 * 0.3f && v < localPeak * 0.8f) { ++count; rising = false; }
            }
            else if (v < localPeak * 0.35f) { rising = true; localPeak = v; }
            else if (v > localPeak) localPeak = v; // still the same burst
        }
        dbg.clapPeaks = count;
    }

    // end level: does the sound run right up to the end of the file? (typical for cut loops)
    float endLevel = 0;
    for (int b = juce::jmax (0, numBlk - 20); b < numBlk; ++b) endLevel = juce::jmax (endLevel, blockPeak[(size_t) b]);
    const bool runsToEnd = endLevel > peak * 0.05f;

    // ---------- analysis-rate mono ----------
    auto x = resampleMono (audio, sr, kAnalysisRate, n);
    const double ar = kAnalysisRate;
    const int xOnset = (int) (out.onsetSec * ar);
    const int xEnd = juce::jmin ((int) x.size(), (int) (out.tailSec * ar) + 1);
    {
        double s = 0; int cnt = 0;
        for (int i = xOnset; i < xEnd; ++i) { s += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
        out.rmsDb = cnt > 0 ? toDb ((float) std::sqrt (s / cnt)) : -200;
    }
    const float crestDb = out.peakDb - out.rmsDb;

    // ---------- spectral features + chroma ----------
    std::array<float, 12> chroma {};
    double centroidAcc = 0, flatAcc = 0, weightAcc = 0, lowAcc = 0, lowMidAcc = 0, airAcc = 0, hiAcc = 0, totalAcc = 0;
    double tuneSin = 0, tuneCos = 0;
    double firstThird = 0, lastThird = 0;
    {
        Spectrum spec (11); // 2048
        const int N = spec.size, hop = 512, half = N / 2;
        const int specEnd = juce::jmin (xEnd, xOnset + (int) (20.0 * ar));
        float frameEnergyMax = 0;
        std::vector<float> energies;
        for (int s = xOnset; s + hop < specEnd || s == xOnset; s += hop)
        {
            double e = 0;
            for (int i = s; i < juce::jmin ((int) x.size(), s + N); ++i) e += (double) x[(size_t) i] * x[(size_t) i];
            energies.push_back ((float) e);
            frameEnergyMax = juce::jmax (frameEnergyMax, (float) e);
            if (s + hop >= specEnd) break;
        }
        int fi = 0;
        const int numFrames = (int) energies.size();
        for (int s = xOnset; fi < numFrames; s += hop, ++fi)
        {
            float e = energies[(size_t) fi];
            if (fi < numFrames / 3) firstThird += e;
            if (fi >= numFrames * 2 / 3) lastThird += e;
            if (e < frameEnergyMax * 1e-4f) continue; // -40 dB frames ignored
            const float* mag = spec.compute (x, s);
            double pSum = 0, cSum = 0, logSum = 0, flatBins = 0, flatPow = 0;
            double low = 0, lowMid = 0, air = 0, hi = 0;
            float maxMag = 0;
            for (int k = 1; k < half; ++k) maxMag = juce::jmax (maxMag, mag[k]);
            for (int k = 1; k < half; ++k)
            {
                float f = (float) (k * ar / N);
                double p = (double) mag[k] * mag[k];
                pSum += p; cSum += p * f;
                if (f < 150) low += p;
                if (f >= 150 && f < 800) lowMid += p;
                if (f >= 6000) hi += p;
                if (f >= 8000) air += p;
                if (f >= 100 && f <= 8000) { logSum += std::log (p + 1e-12); flatPow += p; flatBins += 1; }
            }
            if (pSum <= 0) continue;
            double w = std::sqrt (e);
            centroidAcc += w * (cSum / pSum);
            double gm = std::exp (logSum / juce::jmax (1.0, flatBins)), am = flatPow / juce::jmax (1.0, flatBins);
            flatAcc += w * (am > 0 ? gm / am : 0);
            weightAcc += w;
            lowAcc += low; lowMidAcc += lowMid; airAcc += air; hiAcc += hi; totalAcc += pSum;

            // chroma from spectral peaks between 55 Hz and 5 kHz
            for (int k = 2; k < half - 1; ++k)
            {
                float f = (float) (k * ar / N);
                if (f < 55 || f > 5000) continue;
                float m0 = mag[k - 1], m1 = mag[k], m2 = mag[k + 1];
                if (m1 <= m0 || m1 < m2 || m1 < maxMag * 0.05f) continue;
                float denom = m0 + m2 - 2 * m1;
                float delta = std::abs (denom) > 1e-12f ? 0.5f * (m0 - m2) / denom : 0.0f;
                float fp = (float) ((k + delta) * ar / N);
                float midi = freqToMidi (fp);
                int nearest = (int) std::round (midi);
                float dev = midi - (float) nearest;
                float wgt = std::sqrt (m1);
                chroma[(size_t) (((nearest % 12) + 12) % 12)] += wgt;
                tuneSin += wgt * std::sin (2 * kPi * dev);
                tuneCos += wgt * std::cos (2 * kPi * dev);
            }
        }
    }
    out.centroidHz = weightAcc > 0 ? (float) (centroidAcc / weightAcc) : 0;
    out.flatness = weightAcc > 0 ? (float) (flatAcc / weightAcc) : 0;
    const float lowFrac = totalAcc > 0 ? (float) (lowAcc / totalAcc) : 0;
    const float lowMidFrac = totalAcc > 0 ? (float) (lowMidAcc / totalAcc) : 0;
    const float airFrac = totalAcc > 0 ? (float) (airAcc / totalAcc) : 0;
    const float hiFrac = totalAcc > 0 ? (float) (hiAcc / totalAcc) : 0;
    const float tuningSemis = (float) (std::atan2 (tuneSin, tuneCos) / (2 * kPi));
    dbg.lowFraction = lowFrac; dbg.airFraction = airFrac;

    // ---------- pitch (YIN) ----------
    std::vector<float> voicedMidi;
    int activeFrames = 0;
    {
        Yin yin;
        const int hop = 512;
        const int pEnd = juce::jmin (xEnd, xOnset + (int) (12.0 * ar));
        // frame energy threshold relative to loudest frame
        std::vector<float> fe;
        for (int s = xOnset; s < pEnd; s += hop)
        {
            double e = 0;
            for (int i = s; i < juce::jmin ((int) x.size(), s + Yin::W); ++i) e += (double) x[(size_t) i] * x[(size_t) i];
            fe.push_back ((float) e);
        }
        float feMax = fe.empty() ? 0 : *std::max_element (fe.begin(), fe.end());
        int idx = 0;
        // skip the first 15 ms (transient) for pitch
        for (int s = xOnset; s < pEnd; s += hop, ++idx)
        {
            if (fe[(size_t) idx] < feMax * 0.003f) continue; // -25 dB
            ++activeFrames;
            if (s < xOnset + (int) (0.015 * ar)) continue;
            float ap = 1;
            float f0 = yin.detect (x, s, ar, ap);
            if (f0 > 30 && f0 < 1500 && ap < 0.25f) voicedMidi.push_back (freqToMidi (f0));
        }
    }
    const float voicedFrac = activeFrames > 0 ? (float) voicedMidi.size() / (float) activeFrames : 0;
    float medMidi = voicedMidi.empty() ? -1 : median (voicedMidi);
    float pitchStd = 0;
    int distinctNotes = 0;
    std::array<float, 12> noteHist {};
    if (! voicedMidi.empty())
    {
        // fold octave errors towards the median before measuring spread
        double acc = 0;
        std::map<int, int> hist;
        for (auto& m : voicedMidi)
        {
            float v = m;
            while (v - medMidi > 9) v -= 12;
            while (medMidi - v > 9) v += 12;
            acc += (v - medMidi) * (v - medMidi);
            hist[(int) std::round (m)]++;
            noteHist[(size_t) ((((int) std::round (m)) % 12 + 12) % 12)] += 1.0f;
        }
        pitchStd = (float) std::sqrt (acc / voicedMidi.size());
        for (auto& [note, count] : hist)
            if (count >= juce::jmax (2, (int) (0.12 * voicedMidi.size()))) ++distinctNotes;
    }
    dbg.voicedFraction = voicedFrac; dbg.pitchStdSemis = pitchStd; dbg.distinctNotes = distinctNotes;

    // polyphony from chroma: number of pitch classes above 45% of the strongest
    float chromaMax = *std::max_element (chroma.begin(), chroma.end());
    float chromaSum = std::accumulate (chroma.begin(), chroma.end(), 0.0f);
    int polyphony = 0;
    for (auto c : chroma) if (chromaMax > 0 && c > 0.45f * chromaMax) ++polyphony;
    dbg.polyphony = polyphony;
    auto chromaKey = theory::estimateKey (chroma);

    // ---------- rhythm: onset envelope + autocorrelation ----------
    float rhythmStrength = 0, acfBpm = 0, onsetRate = 0;
    {
        Spectrum spec (10); // 1024
        const int hop = 256, half = spec.size / 2;
        const int rEnd = juce::jmin (xEnd, xOnset + (int) (30.0 * ar));
        std::vector<float> prev ((size_t) half, 0.0f), flux;
        for (int s = xOnset; s + spec.size <= rEnd; s += hop)
        {
            const float* mag = spec.compute (x, s);
            float f = 0;
            for (int k = 1; k < half; ++k)
            {
                float lm = std::log1p (100.0f * mag[k]);
                f += juce::jmax (0.0f, lm - prev[(size_t) k]);
                prev[(size_t) k] = lm;
            }
            flux.push_back (f);
        }
        const double fps = ar / hop;
        if (flux.size() > 16)
        {
            // onset count (peaks above adaptive threshold)
            float fmean = std::accumulate (flux.begin(), flux.end(), 0.0f) / (float) flux.size();
            float fmax = *std::max_element (flux.begin(), flux.end());
            int onsets = 0;
            for (size_t i = 1; i + 1 < flux.size(); ++i)
                if (flux[i] > flux[i - 1] && flux[i] >= flux[i + 1] && flux[i] > fmean * 1.5f && flux[i] > fmax * 0.15f) ++onsets;
            onsetRate = (float) (onsets / (flux.size() / fps));

            std::vector<float> o (flux.size());
            for (size_t i = 0; i < flux.size(); ++i) o[i] = flux[i] - fmean;
            auto acf = [&] (double lag) {
                int l0 = (int) lag; float fr = (float) (lag - l0);
                double s = 0;
                for (size_t i = 0; i + (size_t) l0 + 1 < o.size(); ++i)
                    s += o[i] * (o[i + (size_t) l0] * (1 - fr) + o[i + (size_t) l0 + 1] * fr);
                return s / (double) o.size();
            };
            double a0 = acf (0);
            if (a0 > 0 && flux.size() / fps > 1.5)
            {
                double bestScore = -1e9; double best = 0;
                for (double bpm = 60; bpm <= 200; bpm += 0.5)
                {
                    double lag = 60.0 * fps / bpm;
                    if (lag * 2 >= o.size()) continue;
                    double sc = acf (lag) + 0.5 * acf (lag * 2) + (lag * 4 < o.size() ? 0.25 * acf (lag * 4) : 0) + 0.25 * acf (lag / 2);
                    double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.5, 2));
                    sc = sc / a0 * (0.6 + 0.4 * prior);
                    if (sc > bestScore) { bestScore = sc; best = bpm; }
                }
                acfBpm = (float) best;
                rhythmStrength = (float) juce::jlimit (0.0, 1.0, bestScore / 1.6);
            }
        }
    }
    dbg.rhythmStrength = rhythmStrength; dbg.acfBpm = acfBpm; dbg.onsetRate = onsetRate;

    // ---------- loop / one-shot ----------
    const bool percussiveShape = out.attackMs < 25 && (out.decaySec < 0.6f || effLen < 1.2);
    {
        float score = 0;
        if (hints.saysLoop) score += 2.5f;
        if (hints.saysOneShot) score -= 3.0f;
        if (hints.explicitBpm > 0) score += 1.0f;
        if (effLen >= 1.8 && rhythmStrength > 0.25f && onsetRate > 1.0f) score += 1.5f;
        if (runsToEnd && effLen > 1.5) score += 1.0f;
        if (effLen < 1.0) score -= 2.0f;
        if (percussiveShape && onsetRate < 1.5f) score -= 1.5f;
        if (acfBpm > 0 && effLen > 1.5)
        {
            double beats = (out.lengthSec - out.onsetSec) * acfBpm / 60.0;
            double bars = beats / 4.0;
            if (bars >= 0.9 && std::abs (bars - std::round (bars)) < 0.06) score += 1.0f;
        }
        out.autoType = score >= 2.0f ? SoundType::Loop : SoundType::OneShot;
    }
    const bool isLoop = out.autoType == SoundType::Loop;

    // ---------- tempo ----------
    out.autoBpm = 0;
    {
        auto near = [] (double a, double b, double tol) { return a > 0 && b > 0 && std::abs (a / b - 1.0) < tol; };
        auto octaveNear = [&] (double a, double b, double tol) { return near (a, b, tol) || near (a * 2, b, tol) || near (a / 2, b, tol); };
        const double loopLen = out.lengthSec - out.onsetSec;
        auto barExact = [&] (double bpm) {
            double bars = loopLen * bpm / 240.0;
            return bars >= 0.95 && std::abs (bars - std::round (bars)) / std::round (bars) < 0.015;
        };

        double bpm = 0;
        if (hints.explicitBpm > 0) bpm = hints.explicitBpm;
        else if (isLoop || (effLen > 3.0 && ! percussiveShape))
        {
            if (hints.projectBpm > 0 && (barExact (hints.projectBpm) || octaveNear (acfBpm, hints.projectBpm, 0.05)))
                bpm = hints.projectBpm;
            if (bpm <= 0)
                for (auto c : hints.bpmCandidates)
                    if (octaveNear (acfBpm, c, 0.05) || barExact (c)) { bpm = c; break; }
            if (bpm <= 0 && isLoop && loopLen > 0.5)
            {
                // snap to a whole number of beats/bars close to the rhythmic estimate
                double bestDiff = 1e9;
                for (int beats : { 2, 4, 8, 16, 32, 64, 128 })
                {
                    double t = 60.0 * beats / loopLen;
                    if (t < 70 || t > 180) continue;
                    double ref = acfBpm > 0 ? acfBpm : 120.0;
                    double diff = std::abs (std::log2 (t / ref));
                    // allow double/half time agreement
                    diff = juce::jmin (diff, std::abs (std::log2 (t / (ref * 2))), std::abs (std::log2 (t / (ref / 2))));
                    if (diff < bestDiff) { bestDiff = diff; bpm = t; }
                }
                if (bestDiff > 0.06 && acfBpm > 0 && rhythmStrength > 0.3f) bpm = acfBpm;
                else if (bestDiff > 0.06 && rhythmStrength <= 0.3f) bpm = (bestDiff < 0.2 ? bpm : 0);
            }
            if (bpm <= 0 && hints.projectBpm > 0 && effLen > 3.0) bpm = hints.projectBpm;
            if (bpm <= 0 && isLoop && rhythmStrength > 0.35f) bpm = acfBpm;
        }
        out.autoBpm = bpm > 0 ? std::round (bpm * 100.0) / 100.0 : 0;
    }

    // ---------- tonality ----------
    const bool noisy = out.flatness > 0.3f;
    const bool percussiveNoise = percussiveShape && out.flatness > 0.12f;
    Tonality tonality = Tonality::Atonal;
    float minVoiced = percussiveShape ? 0.3f : 0.4f;
    const bool rhythmicNoisy = isLoop && onsetRate > 2.5f && (out.flatness > 0.12f || polyphony >= 8);
    if (voicedFrac >= minVoiced && voicedMidi.size() >= 3 && ! rhythmicNoisy)
    {
        if (polyphony >= 3 && chromaKey.score > 0.5f && ! percussiveShape) tonality = Tonality::Chords;
        else if (distinctNotes <= 1 && pitchStd < 0.8f) tonality = Tonality::SingleNote;
        else tonality = Tonality::Melody;
    }
    else if (! percussiveNoise && ! noisy && ! rhythmicNoisy && chromaKey.score > 0.55f && polyphony >= 2 && chromaSum > 0)
        tonality = Tonality::Chords;
    // A kick/tom body is a single note even if YIN only catches part of it
    if (tonality == Tonality::Atonal && percussiveShape && voicedMidi.size() >= 2 && pitchStd < 1.0f && ! percussiveNoise)
        tonality = Tonality::SingleNote;
    // pitch sweeps on short hits (kicks, toms, zaps) are still one note
    if (tonality == Tonality::Melody && percussiveShape && effLen < 1.0 && ! isLoop)
        tonality = Tonality::SingleNote;
    // YIN often locks to a sub-harmonic on chords, so don't use its pitch for register
    if (tonality == Tonality::Chords) medMidi = -1;
    out.tonality = tonality;
    out.medianPitchMidi = medMidi;
    out.pitchConfidence = juce::jlimit (0.0f, 1.0f, voicedFrac * (1.0f - juce::jmin (1.0f, pitchStd / 3.0f)));

    // ---------- key ----------
    out.autoKeyRoot = -1; out.autoKeyMode = Major; out.keyConfidence = 0;
    std::array<float, 24> keyScores {};
    if (tonality == Tonality::SingleNote)
    {
        out.autoKeyRoot = (((int) std::round (medMidi)) % 12 + 12) % 12;
        out.autoKeyMode = NoteOnly;
        out.keyConfidence = out.pitchConfidence;
    }
    else if (tonality == Tonality::Melody || tonality == Tonality::Chords)
    {
        std::array<float, 12> profile {};
        float hs = std::accumulate (noteHist.begin(), noteHist.end(), 0.0f);
        for (int i = 0; i < 12; ++i)
        {
            float c = chromaSum > 0 ? chroma[(size_t) i] / chromaSum : 0;
            float h = hs > 0 ? noteHist[(size_t) i] / hs : 0;
            profile[(size_t) i] = tonality == Tonality::Melody ? (0.65f * h + 0.35f * c) : c;
        }
        auto k = theory::estimateKey (profile, &keyScores);
        dbg.keyScore = k.score;
        if (k.score > 0.4f)
        {
            out.autoKeyRoot = k.root; out.autoKeyMode = k.mode;
            out.keyConfidence = juce::jlimit (0.0f, 1.0f, (k.score - 0.4f) * 2.0f + k.margin);
        }
        // project naming as a prior
        if (hints.projectKeyRoot >= 0)
        {
            float pScore = keyScores[(size_t) theory::keyIndex (hints.projectKeyRoot, hints.projectKeyMode)];
            if (out.autoKeyRoot < 0 || pScore > k.score - 0.12f || out.keyConfidence < 0.3f)
            {
                out.autoKeyRoot = hints.projectKeyRoot; out.autoKeyMode = hints.projectKeyMode;
                out.keyConfidence = juce::jmax (out.keyConfidence, 0.5f);
            }
        }
    }
    // An explicit key in the file or folder name wins
    if (hints.fileKeyRoot >= 0)
    {
        if (tonality == Tonality::SingleNote && theory::scaleContains (hints.fileKeyRoot, hints.fileKeyMode, out.autoKeyRoot))
            ; // keep the detected note; it fits the named key
        else
        {
            out.autoKeyRoot = hints.fileKeyRoot; out.autoKeyMode = hints.fileKeyMode; out.keyConfidence = 0.9f;
        }
    }

    // ---------- sampler root note ----------
    out.autoRootMidi = -1;
    if (tonality == Tonality::SingleNote && medMidi > 0)
    {
        // use the median of voiced frames near the main note for cents accuracy
        std::vector<float> nearMain;
        for (auto m : voicedMidi) if (std::abs (m - medMidi) < 0.6f) nearMain.push_back (m);
        out.autoRootMidi = nearMain.empty() ? medMidi : median (nearMain);
    }
    else if ((tonality == Tonality::Melody || tonality == Tonality::Chords) && out.autoKeyRoot >= 0)
    {
        float centre = medMidi > 0 ? medMidi : 64.0f;
        float best = 60; float bestD = 1e9;
        for (int oct = 0; oct < 11; ++oct)
        {
            float cand = (float) (oct * 12 + out.autoKeyRoot);
            if (std::abs (cand - centre) < bestD) { bestD = std::abs (cand - centre); best = cand; }
        }
        out.autoRootMidi = best + tuningSemis;
    }

    // ---------- instrument ----------
    Instrument featInst = Instrument::Unknown; float featConf = 0.3f;
    {
        const float c = out.centroidHz;
        const bool tonal = tonality != Tonality::Atonal;
        if (isLoop)
        {
            if (! tonal || rhythmicNoisy || (onsetRate > 3.0f && out.flatness > 0.08f && voicedFrac < 0.5f))
                { featInst = Instrument::DrumLoop; featConf = 0.6f; }
            else if (medMidi > 0 && medMidi < 50) { featInst = Instrument::Bass; featConf = 0.55f; }
            else if (tonality == Tonality::Chords) { featInst = out.attackMs > 60 ? Instrument::Pad : Instrument::Keys; featConf = 0.35f; }
            else if (medMidi >= 64) { featInst = Instrument::Lead; featConf = 0.3f; }
            else { featInst = Instrument::Synth; featConf = 0.3f; }
        }
        else if (percussiveShape)
        {
            if (lowFrac > 0.5f && c < 700)
            {
                if (tonality == Tonality::SingleNote && out.decaySec > 0.45f && medMidi > 0 && medMidi < 52)
                    { featInst = Instrument::Bass; featConf = 0.45f; }
                else { featInst = Instrument::Kick; featConf = 0.7f; }
            }
            else if (c > 5500 && lowFrac < 0.1f && (tonality == Tonality::Atonal || out.flatness > 0.15f))
                { featInst = effLen > 0.9 ? Instrument::Cymbal : Instrument::HiHat; featConf = 0.65f; }
            else if (out.flatness > 0.1f || noisy || polyphony >= 7)
            {
                if (dbg.clapPeaks >= 3) { featInst = Instrument::Clap; featConf = 0.55f; }
                else if (c > 700 && c < 6000) { featInst = Instrument::Snare; featConf = 0.5f; }
                else { featInst = Instrument::Percussion; featConf = 0.4f; }
            }
            else if (tonality == Tonality::SingleNote && medMidi > 0 && medMidi < 62 && c < 1500 && lowFrac > 0.15f)
                { featInst = Instrument::Tom; featConf = 0.4f; }
            else if (tonal && out.flatness < 0.06f && medMidi >= 50)
                { featInst = Instrument::Pluck; featConf = 0.35f; }
            else { featInst = Instrument::Percussion; featConf = 0.35f; }
        }
        else
        {
            if (! tonal)
            {
                bool rising = lastThird > firstThird * 1.5;
                if (rising && effLen > 1.0) { featInst = Instrument::FX; featConf = 0.4f; }
                else if (effLen > 2.0) { featInst = Instrument::Texture; featConf = 0.35f; }
                else { featInst = Instrument::FX; featConf = 0.3f; }
            }
            else if (medMidi > 0 && medMidi < 50) { featInst = Instrument::Bass; featConf = 0.55f; }
            else if (out.attackMs > 80) { featInst = Instrument::Pad; featConf = 0.45f; }
            else if (tonality == Tonality::Chords) { featInst = Instrument::Keys; featConf = 0.35f; }
            else if (out.decaySec < 1.0f && effLen < 2.5) { featInst = Instrument::Pluck; featConf = 0.35f; }
            else if (tonality == Tonality::Melody && medMidi >= 64) { featInst = Instrument::Lead; featConf = 0.3f; }
            else { featInst = Instrument::Synth; featConf = 0.3f; }
        }
    }
    auto nameGuess = names::instrumentFromWords (hints.fileTokens, hints.folderTokens);
    out.instrumentFromName = false;
    if (nameGuess.instrument != Instrument::Unknown && (nameGuess.strength >= 2 || featConf < 0.6f))
    {
        Instrument inst = nameGuess.instrument;
        // "drums" folder holding a single hit: trust the audio's drum-part guess
        if (inst == Instrument::DrumLoop && ! isLoop && isDrum (featInst) && featInst != Instrument::DrumLoop) inst = featInst;
        out.autoInstrument = inst;
        out.instrumentConfidence = nameGuess.strength == 3 ? 0.9f : nameGuess.strength == 2 ? 0.75f : 0.55f;
        out.instrumentFromName = true;
    }
    else
    {
        out.autoInstrument = featInst;
        out.instrumentConfidence = featConf;
    }

    // ---------- character ----------
    juce::uint32 ch = 0;
    const Instrument inst = out.autoInstrument;
    const bool lowInstrument = inst == Instrument::Bass || inst == Instrument::Kick || inst == Instrument::Tom;
    if (out.centroidHz < (lowInstrument ? 250 : 900)) ch |= chDark;
    if (out.centroidHz > (lowInstrument ? 1500 : 3500)) ch |= chBright;
    if (lowMidFrac > 0.35f && out.centroidHz < 2000 && out.flatness < 0.1f) ch |= chWarm;
    if (airFrac > 0.06f && ! (inst == Instrument::HiHat || inst == Instrument::Cymbal)) ch |= chAiry;
    if (crestDb < 7.0f && out.rmsDb > -20 && (hiFrac > 0.02f || out.flatness > 0.05f)) ch |= chSaturated;
    if (out.attackMs < 10 && crestDb > 12 && (percussiveShape || isLoop)) ch |= chPunchy;
    if (out.attackMs > 60) ch |= chSoft;
    if (! lowInstrument && hiFrac < 0.002f && out.sampleRate >= 32000 && out.centroidHz > 1000) ch |= chLoFi;
    if (out.flatness < 0.05f && ! (ch & chSaturated)) ch |= chClean;
    if (out.numChannels >= 2 && out.stereoWidth > 0.3f) ch |= chWide;
    if (out.numChannels == 1 || out.stereoWidth < 0.02f) ch |= chMono;
    if (effLen > 2.0 && out.decaySec > 1.5f) ch |= chSustained;
    if (out.decaySec < 0.25f) ch |= chShortDecay;
    if (out.flatness > 0.3f) ch |= chNoisy;
    out.character = ch;
}
}
