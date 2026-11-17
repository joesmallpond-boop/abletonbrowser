# Bash Browser

**by Kalabash**

A sample browser and sampler plugin for Ableton Live (VST3 / AU, macOS). It scans the folders you choose, analyses every sound, and sorts the whole library with tick boxes: instrument, key, tempo and more. You audition sounds in time with your project, play them on the built-in sampler, then drag them straight into Live's Arrangement or Session view.

## Install

1. Download `Bash-Browser-mac.dmg` from the latest run on the repo's **Actions** tab (Build → Artifacts).
2. Open the DMG and run **Bash Browser Installer.pkg**. It installs:
   - VST3 → `/Library/Audio/Plug-Ins/VST3`
   - AU → `/Library/Audio/Plug-Ins/Components`
   - Standalone app → `/Applications`
3. The installer isn't signed by Apple yet, so macOS may block it the first time. Right-click the .pkg, choose **Open**, then confirm. You can also allow it under System Settings → Privacy & Security.
4. In Live, rescan plugins (Settings → Plug-Ins). Bash Browser appears under **Plug-Ins → Kalabash** as an instrument. Put it on a MIDI track.

## Using it

**Folders.** On first open, the folder picker appears. Add your Ableton projects folder, your SSD, and your sample libraries. Everything inside is analysed in the background, and you can keep working while it scans. New recordings are picked up automatically every few minutes, and the **Rescan** button checks immediately.

**Tick boxes (left).** Within a category, ticks combine with OR (Kick + Snare shows both). Different categories narrow the list (Bass + G min shows bass sounds in G minor). The numbers beside each box show how many sounds you'd get by ticking it.

| Category | What it means |
|---|---|
| Instrument | Kick, Snare, Clap, Hi-hat, Cymbal, Tom, Percussion, Drum loop, Bass, Keys, Guitar, Synth, Pad, Lead, Pluck, Strings, Brass & wind, Vocal, FX, Texture |
| Key | 24 keys plus "No key". Single-note sounds (tuned kicks, one-note basses) go under their note. Tick **+ compatible** to also include relative major/minor keys and single notes that fit the scale |
| Tempo | Ranges, "No tempo", and **Fits project tempo** (within 3%, including double/half time) |
| Type | One-shot or Loop |
| Pitch content | Single note, Melody, Chords, or No clear pitch |
| Register | Sub / Low / Mid / High |
| Character | Dark, Bright, Warm, Airy, Saturated, Punchy, Soft, Lo-fi, Clean, Wide, Mono, Sustained, Short decay, Noisy |
| Length | From under 0.25 s to over 15 s |
| Style | Genre and vibe words found in file, folder and project names (Dub, Techno, Lo-fi...), plus your own |
| My tags | Your own tags |
| Project / source | The Ableton project (or library folder) the sound came from |

**Auditioning.** Click a sound, or move through the list with the arrow keys, and it plays. When Live is playing, the preview waits for the next bar by default, so it lands on the one. **Start** can also be set to *Next beat*, *Immediately*, or *In phase*, which jumps straight into a loop at the matching point. **Warp** follows the project tempo: *Stretch* keeps the pitch, *Re-Pitch* changes speed like a record, *Off* plays as recorded. Press **Space** to play or stop.

**Getting sounds into Live.**
- Drag a row from the list, or the orange **DRAG TO LIVE** handle, onto the Arrangement or Session view, or onto a Simpler or Sampler.
- The menu at the top right picks what you drag: the **original file**, or a **trimmed, tempo-matched** copy (leading silence removed, warped to your project tempo, cut to whole beats). Rendered copies are saved in `~/Library/Application Support/Bash Browser/Rendered`.

**Sampler (bottom).** With **To sampler** ticked, the selected sound is ready to play straight away from a MIDI keyboard or the on-screen keys. The detected root note is marked in green on the keyboard. It's mapped automatically, so C plays C. Controls follow Ableton's Simpler:
- **Classic or One-Shot** mode, with draggable Start/End markers on the waveform.
- **Auto start**, which skips leading silence.
- **Loop**, with loop position and crossfade.
- **Amp ADSR**, plus fade in/out for one-shots.
- **Filter** (LP/HP/BP, 12 or 24 dB) with its own ADSR, key tracking and velocity.
- **LFO** to pitch, filter, volume and pan, with tempo sync.
- **Transpose, detune, glide and voice count.**

The sampler's sound and settings are saved with your Live set.

**Fixing tags (right).** Change the instrument, key, tempo (including ½× and 2×), type and sampler root note, mark favourites, and add styles and tags. Edited values show in orange in the list. Your edits survive rescans. **Reset to detected** undoes them. Right-click any row for quick fixes.

## How the analysis works

- **Pitch:** YIN pitch tracking, used for single notes, melodies and the sampler root note (with cents).
- **Key:** chroma from spectral peaks, matched against Krumhansl–Kessler key profiles.
- **Tempo:** onset-strength autocorrelation, snapped to whole bars, and cross-checked with the BPM in the file name. For sounds inside an Ableton project, it uses the tempo stored in the project's `.als` set.
- **Names:** keys and styles in project folder names (e.g. "Gm dub Project") apply to the sounds inside that project.
- **Instrument:** keywords in file and folder names come first. Otherwise it's classified from the sound: attack, decay, spectral balance, noisiness, register and multiple-burst detection for claps.

All of this runs locally. The library is stored in `~/Library/Application Support/Bash Browser/`.

## Building

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BashBrowser_VST3 BashBrowser_AU BashBrowser_Standalone
```

`AnalyzerTests` and `EngineTests` are test programs. `AnalyzerTests <folder>` prints the tags for every sound in a folder, which is useful for checking the analysis on real material.

Uses [JUCE](https://juce.com) and [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT) for time-stretching.
