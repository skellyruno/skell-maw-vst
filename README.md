# Skell Maw - The Girl & The Serpent (plugin collection)

Seven JUCE 8 audio plugins that all use one illustration as their interface. **The Windows release workflow builds and packages VST3 only.** The optional Apple development configuration adds AU; this CMake project does not configure a Standalone target.
Built on the StopDial quickstart template: same CMake + FetchContent JUCE setup, same vector-only, no-bundled-fonts approach.

| # | Plugin | Type | Hero gesture on the art |
|---|---|---|---|
| 1 | Dark Forest | Reverb | Drag the tree trunks up/down: room size |
| 2 | Hypnosis | Phaser | Drag the serpent's eye up/down: LFO rate (the glow pulses with the LFO) |
| 3 | Serpent's Maw | Distortion | Pull the tongue left: drive |
| 4 | Butterfly Delay | Ping-pong delay | Drag the butterflies left/right: delay time (or pick a tempo sync) |
| 5 | Shed Skin | Freeze / granular glitch | Click the serpent to freeze, drag to set grain size |
| 6 | Constrictor | Compressor + soft/hard ceiling | Pull the rope down: lower threshold (rope glows with gain reduction) |
| 7 | Night Bloom | Harmonic exciter | Drag the blossom branch: bloom amount |

Shift-drag = fine adjust. Double-click the hotspot = reset. Hamburger (top-left) = reset all controls / about.

## Build

Requires CMake 3.22+, a C++17 compiler, and internet on first configure (CMake downloads JUCE 8.0.12),
or pass `-DJUCE_PATH=/path/to/JUCE`.

```bash
# Windows (Visual Studio 2022)
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 2

# macOS
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --parallel 3
```

Outputs land in `build/<Plugin>_artefacts/Release/...`. Build one plugin only with
`cmake --build build --config Release --target SerpentsMaw`.

### No local toolchain? Use GitHub

Push this folder to a GitHub repo and run **Actions -> Build Windows VST3**.
Download `SkellMaw-Windows-VST3` from the workflow run. This artifact is a preview package, not an installer or commercial release; use the included `WINDOWS-VST3-INSTALL.txt` for safe manual test installation and rollback.

## Layout

```
Assets/art.jpg           your illustration (embedded into every plugin)
Common/                  shared library used by all plugins
  PluginSpec.h           describes a plugin: params, art crop, hero hotspot
  MawProcessorBase.*     APVTS built from the spec, state, level metering
  MawEditor.*            art-based editor: knobs, dropdowns, hero drag, glow
  MawLookAndFeel.*       rotary knobs, pill toggle, popup menu
  DspUtil.h              delay line, one-pole, biquad
Plugins/<Name>/<Name>.cpp   spec + DSP for each plugin (~100 lines each)
```

## Adding chapter 8

1. Copy any `Plugins/X/X.cpp`, rename, change the spec and DSP.
2. Add one `maw_add_plugin(...)` line in `CMakeLists.txt` with a unique 4-char `PLUGIN_CODE`.

## Tuning the art

Each plugin shows a crop of the illustration (`p.crop`) and a hero hotspot (`p.heroRect`),
both in fractions of the original image. If a hotspot or glow sits in the wrong place,
edit those numbers in the plugin's `makeSpec()`. Hover the art to see the hotspot outline.
The source image is only ~750 px, so tight crops (Hypnosis, Night Bloom) look soft;
replace `Assets/art.jpg` with a high-res export (same framing) for sharp results.

## Status and known limits

- The Windows release workflow builds the x64 VST3 collection, validates the staged bundles, and runs pluginval strictness 5 for all seven plugins. A green CI result does **not** establish FL Studio host acceptance, saved-project/state recall, real audio A/B, target-hardware CPU/latency, or full UI acceptance; verify those separately.
- The Actions package is preview-only and unsigned; it is not an installer or commercial release. Verify the exact workflow head and included SHA-256 manifest before installing.
- No presets, bypass button, or resizable-art animation yet.
- Night Bloom, Constrictor and the Tube/Fold/Hard modes of Serpent's Maw are not oversampled
  (only the Serpent's Maw shaper is 4x oversampled); add oversampling if you hear aliasing.
- Shed Skin is a time-domain granular freeze, not an FFT spectral freeze.
- Hosts that exceed their declared max block size make Dark Forest pass audio through.
- Company name "Skell Audio", manufacturer code `SkAu` and bundle IDs are taken from the
  template; change them in `CMakeLists.txt` if you want your own.
- Commercial release still needs owner-approved license/attribution, vendor URL and support email, a Windows signing/installer decision, and host/audio acceptance. If macOS distribution is pursued later, notarization is a separate requirement.

## DSP tests (no JUCE needed)

Build the actual Constrictor, Night Bloom, Butterfly Delay and Shed Skin processing
sources against the small JUCE shim and run their regression checks via CTest:

```bash
cmake -S . -B build-dsp -DMAW_DSP_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp --output-on-failure
```

These tests cover audio bounds and finite values, delay timing, freeze transitions,
and Night Bloom's parameter-automation smoothing. They are **not** a substitute for
building JUCE VST3s, checking mono and stereo hosts, recording real A/B audio,
or measuring Windows CPU/latency.

`Tests/` compiles the real Constrictor, Night Bloom, Butterfly Delay and Shed Skin sources against a tiny JUCE stand-in and checks levels, timing, NaNs, clicks and bounds:

    g++ -std=c++17 -fsanitize=address,undefined -ITests/shim Tests/harness.cpp Plugins/Constrictor/Constrictor.cpp -o t && ./t constrictor

Names: constrictor, nightbloom (Plugins/NightBloom), butterfly (ButterflyDelay), shed (ShedSkin).

## Animation layers
The six butterflies are cut out of the illustration and animated live (flap, drift, faster when audio is playing). `Assets/art.jpg` is the art *without* them; `Assets/butterflies.png` holds the sprites; the untouched original is `Assets/source/art_original.jpg`. To change the art, replace the original and run `python3 Tools/make_layers.py` (numpy, scipy, opencv-python, pillow). If the butterflies move in the new art, update the home positions in `kSprites` in `Common/MawEditor.cpp` from the script's printed table.

## What moves in each plugin
All: breathing zoom, flapping butterflies, drifting particles, an idling tongue. Plus:
Dark Forest - mist rolls in with Size and level. Hypnosis - the pupil dilates with the real LFO and rings leave the eye.
Serpent's Maw - the tongue flicks, faster the hotter the signal. Butterfly Delay - ghost trails behind the butterflies, spaced by Time.
Shed Skin - freezing slows the whole picture and ices the corners. Constrictor - the frame closes in with gain reduction.
Night Bloom - the blossoms glitter with Amount. (`Assets/tongue.png` is the tongue cut out of the art by Tools/make_layers.py.)
