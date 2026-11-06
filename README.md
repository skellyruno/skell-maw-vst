# Skell Maw - The Girl & The Serpent (plugin collection)

Seven JUCE 8 audio plugins (VST3 + Standalone, plus AU on macOS) that all use one illustration as their interface.
Built on the StopDial quickstart template: same CMake + FetchContent JUCE setup, same GitHub Actions flow,
same vector-only, no-bundled-fonts approach.

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

Push this folder to a GitHub repo and run **Actions -> Build Skell Maw collection**.
Download `SkellMaw-Windows` / `SkellMaw-macOS` from the run's artifacts.

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

- **Not yet compiled.** This was written without a JUCE install or network, so the first
  CI run may surface a typo or two. Send me the compiler output and I'll fix it.
- No presets, bypass button, or resizable-art animation yet.
- Night Bloom, Constrictor and the Tube/Fold/Hard modes of Serpent's Maw are not oversampled
  (only the Serpent's Maw shaper is 4x oversampled); add oversampling if you hear aliasing.
- Shed Skin is a time-domain granular freeze, not an FFT spectral freeze.
- Hosts that exceed their declared max block size make Dark Forest pass audio through.
- Company name "Skell Audio", manufacturer code `SkAu` and bundle IDs are taken from the
  template; change them in `CMakeLists.txt` if you want your own.
- Commercial release needs code-signing / notarization on macOS and a VST3 SDK licence check.
