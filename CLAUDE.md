# reLoad: notes for Claude

Wavetable synth plugin (JUCE 8, C++20, CMake) that rebuilds recorded sounds as
wavetables. Full spec and decisions log: `docs/SPEC.md`.

## Standing rules
- **After every completed change to plugin code** (compiles + tests pass), run
  `scripts/build_install_mac.sh`. Report the install paths, build result,
  pluginval result, auval result, and whether a DAW rescan is needed (the script
  prints this).
- **Windows**: describe it as "compiles in CI, untested at runtime" until the
  owner has tested it on a real machine. Never say the Windows build works.
- **Ask before downloading or installing anything.** Dependencies are pinned by
  version + SHA-256 in `CMakeLists.txt` and `.github/workflows/build.yml`.
- **No downloaded test audio.** Generate synthetic signals, or ask the owner for
  samples.
- **Approximation tool.** UI/docs must never imply the plugin identifies the
  real instrument.
- **Original artwork only.** No third-party synth logos, names, or trade dress.

## Engineering rules
- Audio thread: no allocation, no locks, no `juce::MidiMessage` construction
  (parse raw bytes). Wavetables are swapped lock-free; retired objects are freed
  off the audio thread.
- Must be sample-rate and block-size independent (tests check this).
- Parameter IDs in `src/plugin/Parameters.h` are permanent once released. Add
  new IDs; never rename.
- `src/dsp/` and `src/analysis/` must stay platform-neutral and GUI-free.
- Chord import: options (a) lock-to-lowest-note [default], (b) user loop
  length, (c) polyphonic detection that reports the notes found.

## Commands
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build                                        # build all
ctest --test-dir build --output-on-failure                 # tests
./scripts/build_install_mac.sh                             # build, test, validate, install
```
pluginval lives in `tools/pluginval.app` (git-ignored; v1.0.4 from Tracktion's
GitHub releases).
