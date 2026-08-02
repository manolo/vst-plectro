# Plectro VST

A VST3 / AudioUnit **instrument** for the Spanish plucked plectrum family ("pulso y pua":
bandurria, laud). It loads inside MuseScore 4 (or any VST3/AU host) as the sound source for a
staff, plays a bundled SoundFont, and makes the two things MuseScore renders badly on these
instruments, **tremolo** and **trills**, sound like a real player instead of a sequencer. The whole
workflow stays in MuseScore: ordinary tremolo beams and ordinary ornaments, no velocity hacks.


## Tremolo, two techniques

MuseScore renders a measured tremolo as a rapid burst of repeated note ons. Plectro turns that back
into one sustained tremolo voice instead of a machine gun of retriggers, and it can decide when a
note is a tremolo in **two** ways:

- **Keyswitch driven.** The patched MuseScore build advertises and sends a keyswitch per note (via
  the VST3 `IKeyswitchController`), so the articulation is explicit and unambiguous.
- **Automatic detection.** With a plain host, or before a keyswitch arrives, Plectro detects the
  repeated note burst itself (within a configurable window) and collapses it into one tremolo.

Both run today on purpose: the keyswitch path is the intended single source of truth, but a host
side limit on how many keyswitch events a block can deliver can still drop events on dense passages.
Until that keyswitch delivery issue is fixed, Plectro keeps both techniques so tremolo is reliable
with or without the patched host.

Because tremolo no longer has to be encoded in velocity, MuseScore hairpins and dynamics flow
through as real loudness.

## Trills and ornaments

MuseScore also expands a trill (and mordents and turns) into a rapid alternation of note ons, which
on a bandurria or laud sounds mechanical. With **Capture Trills** on, Plectro keeps only the main
note and renders the ornament as a single sustained tremolo, so a trilled long note sounds like a
tremolo roll on the written pitch instead of a stuttering two note trill. Turn it off to hear the
ornament as written notes.

## Sound

The sound comes from a SoundFont played by an embedded FluidSynth, so the font's envelopes, filter,
LFOs and looped tremolo layers are reproduced exactly. Each instrument bank carries its
articulations as named presets (base pick, tremolo, picked tremolo attack, and where present mute,
pizzicato and harmonic), selected by name at load time.

The bundled font ships **bandurria** and **laud**.


## Architecture

```
MuseScore (normal tremolo beams + normal ornaments + normal dynamics)
  -> note events (+ keyswitches when the patched host drives them)
  -> Plectro:
       look-ahead buffer
       -> articulation (keyswitch, or automatic tremolo/trill detection)
       -> note variation
       -> FluidSynth (the SoundFont: pick vs tremolo vs ...)
  -> audio -> host mixer
```

The articulation, detection and streaming logic live in a JUCE free, unit tested static library
(`plectro_core`); the plugin is a thin JUCE wrapper plus a FluidSynth adapter.

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
`plectro_core` tests never need JUCE or FluidSynth, so they are fast:

```sh
cmake -B build -DHVST_BUILD_PLUGIN=OFF
cmake --build build --target core_tests
ctest --test-dir build --output-on-failure
```

## Install

Copy the built bundle into the plugin folder:

- macOS VST3: `~/Library/Audio/Plug-Ins/VST3/`
- macOS AU: `~/Library/Audio/Plug-Ins/Components/`
- Windows VST3: `C:\Program Files\Common Files\VST3\`

## Use in MuseScore 4

1. Open the Mixer (F10), pick the bandurria/laud staff, and set its sound to **Plectro**.
2. Write ordinary tremolo beams on long notes, ordinary trills and ordinary dynamics. The old
   velocity hack and tied note muting are not needed.

## Parameters

Master gain; tremolo (enable, detection window, capture trills); output enable; look-ahead.

## License

See `LICENSE`.
