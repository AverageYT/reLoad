# reLoad

A wavetable synthesizer plugin built around one idea: take a recorded sound (a
one-shot or a chord from a sample) and rebuild it as an editable wavetable you
can play like any other synth patch.

reLoad is an **approximation tool**. It analyses pitch and timbre and builds a
playable patch that resembles the source. It does not and cannot identify the
instrument that made the original sound.

## Status

| Milestone | State |
|---|---|
| M1: skeleton plugin, sine voice, install script, CI | done |
| M2: file import → single-cycle wavetable at correct pitch (+ mipmaps) | done |
| M3: evolving + spectral import, mipmaps, table scanning | planned |
| M4: full synth engine | planned |
| M5: GUI pass | planned |
| M6: reverse-engineering tools | planned |
| M7: FX, presets, polish | planned |

| Platform | Formats | Status |
|---|---|---|
| macOS 11+ (universal arm64 + x86_64) | VST3, AU, Standalone | built, tested and validated locally (pluginval, auval) |
| Windows x64 (MSVC) | VST3, Standalone | **compiles in CI, untested at runtime** |

## Building (macOS)

Requirements: Xcode Command Line Tools, CMake ≥ 3.25, Ninja. JUCE and Catch2 are
fetched by CMake on first configure (pinned versions, SHA-256 checked).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Build, test, validate and install into `~/Library/Audio/Plug-Ins/` in one step:

```bash
./scripts/build_install_mac.sh
```

The script needs [pluginval](https://github.com/Tracktion/pluginval/releases)
v1.0.4 unpacked to `tools/pluginval.app`. It installs only after the build,
tests and a pluginval run on a staged copy all pass. It only ever replaces
`reLoad.vst3` / `reLoad.component`, and it restores the previous version if
validation of the installed copy fails.

## Windows

The GitHub Actions workflow builds the Windows VST3 and Standalone with MSVC,
runs the unit tests and pluginval on the CI runner, and uploads the artifacts
as `reLoad-Windows-untested`. It has not yet been tested on a real Windows
machine or in a Windows DAW.

## License

reLoad is licensed under the [GNU Affero General Public License v3.0](LICENSE).
It is built on [JUCE](https://juce.com), which is used here under its AGPLv3
option.
