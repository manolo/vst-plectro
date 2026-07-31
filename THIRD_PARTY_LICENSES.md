# Third party licenses

Plectro's own source is under the MIT License (see LICENSE). The distributed plugin also includes
or links the following third party components. Their licenses apply to those components.

## Audio framework

- **JUCE** (https://juce.com) — the plugin is built with JUCE, used under JUCE's free tier, which
  is permitted while revenue stays below JUCE's threshold (this project expects at most token
  donations, well within it). The distributed binary combines JUCE, so keep within the free tier's
  obligations (for example JUCE may require a "Made with JUCE" splash and has terms on source
  disclosure). Confirm the current JUCE free tier terms before a public release.

## Bundled native libraries (macOS)

Shipped inside the plugin bundle under `Contents/Frameworks`, linked dynamically. The release build
(`PLECTRO_MINIMAL_FLUIDSYNTH=ON`) builds a minimal FluidSynth so only these ship:

- **FluidSynth** (https://www.fluidsynth.org) — LGPL 2.1 or later. Built from source with readline,
  libsndfile and the audio drivers disabled, so those are NOT bundled.
- **GLib** (libglib-2.0, libgthread-2.0) — LGPL 2.1 or later.
- **gettext runtime** (libintl) — LGPL 2.1 or later.
- **PCRE2** (libpcre2-8) — BSD 3 Clause.

LGPL compliance note: these libraries are shipped as unmodified shared libraries and linked
dynamically, which lets a user replace them. That is the LGPL compliant way to bundle them and
imposes no obligation to open source Plectro's own code.

## SoundFont

- **Plectro.sf2** (the bundled instrument samples) — Creative Commons CC0 1.0 (public domain
  dedication). No attribution required.
