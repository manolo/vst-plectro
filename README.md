# Plectro VST

A VST3 / AudioUnit **instrument** that humanizes plucked plectrum instruments (Spanish
"pulso y pua": bandurria, laud) and plays your own SoundFont. It loads inside MuseScore 4 as
the sound source for a staff, keeps the whole workflow in MuseScore, and makes the
performance sound like a real player instead of a sequencer.

## What it does

MuseScore renders correctly but mechanically: every onset is exactly on the grid and the
dynamics are flat. Plectro fixes that at the note level, where it actually helps:

- **Tremolo by pattern, not by hack.** MuseScore renders a measured tremolo as a rapid burst
  of repeated note-ons. The plugin detects that burst and triggers one sustained tremolo
  sample, instead of a machine-gun of retriggers. A lone note plays the picked sample.
- **Velocity is free for real dynamics again.** Because tremolo is detected from the pattern,
  velocity no longer has to encode articulation, so MuseScore hairpins and dynamics flow
  through as loudness (applied via engine gain / expression). Articulation is sent to the
  SoundFont on the velocity band it expects (1-64 picked, 65-127 tremolo).
- **Timing humanization:** per-note onset jitter plus a slow phrase "breathing" drift.
- **Dynamics humanization:** note-to-note variation and phrase shaping on top of the notated
  dynamic, with the tremolo swell preserved across the burst.
- **Ensemble spread:** put an instance on each of bandurria 1 / 2 / 3 with a different
  *Instance Seed* and the unison section sounds like several players, not one.
- **Reproducible:** a global seed makes every render identical.

The sound comes from your SoundFont (e.g. `Bandurria-Con-Tremolo.sf2`), played by an embedded
FluidSynth so the font's LFOs, envelopes, filter and looped tremolo layers are reproduced
exactly.

## Architecture

```
MuseScore (normal tremolo beams + normal dynamics)
  -> note events (velocity = real dynamics)
  -> Plectro:
       look-ahead buffer
       -> tremolo detector (collapse repeated-note bursts)
       -> humanizer (timing + dynamics + ensemble seed)
       -> FluidSynth (your SF2: picked vs tremolo)
  -> audio -> MuseScore mixer
```

The humanization logic lives in a JUCE-free, unit-tested static library (`humanizer_core`);
the plugin is a thin JUCE wrapper plus a FluidSynth adapter.

## Build

Requirements: CMake 3.22+, a C++20 compiler, and (for sound) FluidSynth.

```sh
# macOS
brew install fluid-synth pkg-config
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure   # runs the core unit tests
```

Without FluidSynth the plugin still builds, but silent (handy for CI smoke builds). The
`humanizer_core` tests never need JUCE or FluidSynth, so they are fast:

```sh
cmake -B build -DHVST_BUILD_PLUGIN=OFF
cmake --build build --target core_tests
ctest --test-dir build --output-on-failure
```

Prebuilt binaries for macOS (VST3 + AU) and Windows (VST3) are produced by the GitHub Actions
matrix, so a Windows VST3 is available without a Windows machine.

## Install

Copy the built bundle into the plugin folder:

- macOS VST3: `~/Library/Audio/Plug-Ins/VST3/`
- macOS AU: `~/Library/Audio/Plug-Ins/Components/`
- Windows VST3: `C:\Program Files\Common Files\VST3\`

## Use in MuseScore 4

1. Open the Mixer (F10), pick the bandurria/laud staff, and set its sound to **Plectro**.
2. Open the plugin UI and point *Load SF2* at your `Bandurria-Con-Tremolo.sf2`.
3. Write ordinary tremolo beams on long notes and ordinary dynamics. The old velocity hack and
   tied-note muting are no longer needed.
4. For a section, duplicate the part across staves and give each instance a different
   *Instance Seed*.

## Parameters

Master gain; tremolo detection (window, min repeats, on/off); timing (jitter, breathing depth
and rate); dynamics (velocity curve, variation); length variation; articulation velocity bands
(picked / tremolo); reproducibility (global seed, instance seed); look-ahead.

## License

See `LICENSE`.
