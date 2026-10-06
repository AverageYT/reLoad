# reLoad: project specification

Wavetable-extraction synth plugin (macOS + Windows).

## Decisions log

| Date | Decision |
|---|---|
| 2026-10-06 | Plugin name: **reLoad**. AU codes: manufacturer `Rlda`, plugin `Rlod`. Company name `reLoad Audio`, bundle ID `com.reloadaudio.reload` (kept as-is; no company). |
| 2026-10-06 | License: **AGPLv3** (JUCE used under its AGPLv3 option). |
| 2026-10-06 | Chords: all three approaches are user-selectable options. **(a) lock to lowest note is the default.** (b) user-chosen loop/period and (c) polyphonic note detection are both available. |
| 2026-10-06 | GitHub repo is public. |
| 2026-10-06 | Pinned deps: JUCE 8.0.15, Catch2 v3.16.0, pluginval v1.0.4. |

---

## Spec

Edited for the public repo: local paths removed, first-person wording changed
to "the owner", and third-party product names removed. Requirements unchanged.

Build a cross-platform wavetable synthesizer plugin whose defining feature is
converting a recorded instrument sound into an editable wavetable. The purpose:
let a user take a sound from a sample (a chord or one-shot lifted from a song or
recording) and rebuild it as a playable synth patch that approximates the
original instrument. This is an approximation tool; the plugin cannot identify
the real instrument, and the UI/docs should not imply that it can.

### Stack and targets
- C++20, JUCE 8, CMake. Fetch JUCE with CMake FetchContent. Tell the owner before
  downloading or installing anything.
- Formats: VST3 on both platforms, plus AU on macOS (Logic/GarageBand don't load
  VST3), plus a Standalone build for testing without a DAW. No VST2.
- macOS: universal binary (arm64 + x86_64), minimum macOS 11.
  Windows: x64, MSVC.
- Only the Mac version can be built and run locally. Keep all plugin code
  platform-neutral, with a GitHub Actions workflow (macos-latest +
  windows-latest) that builds both and uploads the artifacts. Until it has been
  tested on a real Windows machine, describe the Windows build as "compiles in
  CI, untested at runtime". Never say it works.

### Core workflow
1. User drags an audio file (WAV/AIFF/FLAC/MP3/OGG) onto the plugin, or uses a
   Load button. Input is a one-shot (single note) or a chord.
2. Analysis runs on a background thread and produces a wavetable.
3. The wavetable loads into oscillator A and plays like any normal wavetable
   oscillator: position knob, warp, unison, filter, envelopes, and so on.
4. The source's detected root pitch maps to a MIDI note, so the played pitch
   matches the original.

### Audio -> wavetable
- Table format: frames of 2048 samples, up to 256 frames (the common wavetable
  convention, so exports are interchangeable).
- Import modes (auto-selected by default, user-overridable):
  - Single cycle: detect f0 (YIN/pYIN), extract one period, resample to 2048,
    clean up the loop point.
  - Evolving: track f0 over time, resample each analysis window to exactly one
    period, phase-align neighbours, one frame per slice. The table-position knob
    then sweeps the sound from attack to decay.
  - Spectral: STFT, harmonic magnitudes/phases, inverse FFT per frame. Use this
    for noisy or inharmonic material.
  - Chords: do not pretend a chord has one clean period. Offer (a) lock to the
    lowest detected note and keep its harmonic series, (b) a user-chosen
    loop/period length, and (c) polyphonic note detection that reports the notes
    found. Show which mode was used and why.
- User controls: manual root note / f0, region start/end on the waveform, frame
  count, window size, DC removal, normalize, mono-sum vs. stereo.
- Playback quality: per-octave band-limited mipmaps built via FFT, interpolation
  between frames (linear, with a spectral-morph option), and no audible aliasing
  at high notes.

### Synth engine (build in this priority order)
1. Oscillators A and B: wavetable position, level, pan, octave/semi/fine, phase,
   unison (up to 16 voices with detune, blend, stereo spread), 6+ warp modes
   (bend, sync, FM, mirror, quantize, and so on), sub oscillator, noise.
2. Multimode filter (LP/HP/BP/notch, 12/24 dB, drive).
3. Amp and mod ADSR envelopes, 2-3 LFOs, 4 macros, and a modulation matrix with
   drag-and-drop from a source to any parameter, with amount and polarity.
4. Voice handling: 16-voice polyphony, mono/legato, portamento, pitch bend, mod
   wheel.
5. FX: distortion, chorus, delay, reverb, EQ, compressor.

### Reverse-engineering tools (the point of the plugin)
- Source panel: waveform, spectrogram, detected f0 / note name, harmonic bars.
- A/B compare: play the original and the synth side by side, level-matched, with
  a spectrum overlay of target vs. synth output.
- "Match envelope": extract the amplitude envelope from the source and set the
  amp ADSR; use spectral centroid over time to suggest a filter cutoff/envelope.
- Wavetable editor: per-frame waveform draw, harmonic (spectral) editor, process
  tools (normalize, smooth, spectral tilt), and frame reorder/duplicate/delete.
- Export the wavetable as a 2048-sample-per-frame WAV.

### GUI
- Layout and interaction in the style of modern wavetable synths, with original
  artwork only: no third-party logos, names, or trade dress.
- Dark theme, vector-drawn, custom LookAndFeel, resizable, HiDPI-aware.
- Top bar with preset browser (prev/next/save). Oscillator panels with a 3D
  waterfall view of the wavetable, a 2D waveform, and a position knob. Tabbed
  lower section (mod matrix / FX / global). The import and analysis panel is a
  prominent first-class section, not a hidden menu.
- Double-click to type values, right-click menus, tooltips, undo/redo, and
  drag-and-drop modulation with visible mod-amount rings on knobs.

### State and presets
- Embed the generated wavetable in plugin state, so projects and presets reload
  without the original audio file.
- Expose every parameter to the host via AudioProcessorValueTreeState
  (automatable).

### Engineering requirements
- Real-time safety: no allocation or locks on the audio thread. Swap wavetables
  lock-free. Handle denormals. Be sample-rate and block-size independent.
- Tests: unit tests for the analysis (synthetic sine/saw/chord with known f0 and
  notes), offline render tests, `pluginval` at strictness >= 5 on the VST3, and
  `auval` on the AU.

### Mac install (standing requirement)
- `scripts/build_install_mac.sh` builds Release, copies the VST3 to
  `~/Library/Audio/Plug-Ins/VST3/` and the AU to
  `~/Library/Audio/Plug-Ins/Components/`, ad-hoc codesigns, clears the
  quarantine attribute, and runs pluginval on the installed copy.
- Run it after every completed change to plugin code (each coherent set of edits
  once it compiles and tests pass, not on every file save).
- Report the install path, the build result, and the pluginval result each time.
- If the build or tests fail, do not overwrite the previously installed working
  version. Only replace this plugin's own bundles and never touch other plugins.
- Say when a DAW rescan is needed.

### Milestones
- M1: skeleton plugin builds, installs, and plays a sine; CI green on both OSes.
- M2: file import -> single-cycle wavetable -> plays at the correct pitch.
- M3: evolving + spectral import, mipmaps, table-position scanning.
- M4: full synth engine (unison, filter, envelopes, LFOs, mod matrix).
- M5: GUI pass (layout, 3D wavetable view, drag-drop modulation).
- M6: reverse-engineering tools (compare, envelope match, editor, export).
- M7: FX, presets, polish.

After each milestone: what works, what's untested, and how to try it.
For test audio, generate synthetic signals or ask the owner for real samples.
Don't download any.
