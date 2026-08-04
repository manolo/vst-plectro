# Plectro articulations and keyswitches

<!-- GENERATED FILE - do not edit by hand. -->
<!-- Source of truth: articulations/plectro-articulations.json -->
<!-- Regenerate with: python3 tools/generate_articulation_maps.py -->

Plectro is a VST3 and Audio Unit instrument. It selects its playing techniques
(pizzicato, tremolo, harmonic, mute, legato) through **keyswitches**: low MIDI notes
that pick an articulation instead of sounding a pitch.

## What a keyswitch is

A keyswitch is a MIDI note reserved to switch articulation rather than play a note.
When Plectro receives a note in its reserved low zone (MIDI 0 to 12), it does not sound; it selects the mapped articulation, and every
following musical note plays with that articulation until another keyswitch changes it.

**There is no universal keyswitch standard.** Different libraries put different
articulations on different notes. Plectro publishes one stable map (below) and never
changes the note numbers, so saved projects keep working. Plectro is a self contained
instrument, not a Kontakt library, so it needs no external sample engine.

## The map

The MIDI **number** is the reference. Hosts label the same number differently by octave
convention: MIDI 0 shows as **C-1** in Cubase, Dorico and MuseScore, and as **C-2** in
Logic Pro and GarageBand. Always trust the number.

| MIDI | Note (C-1=0) | Articulation | Internal | Behavior | Note Off | Category |
|-----:|:-------------|:-------------|:---------|:---------|:--------:|:---------|
| 0 | C-1 | Standard | Picked | Latching | no | base |
| 1 | C#-1 | Pizzicato | Pizzicato | Latching | no | base |
| 2 | D-1 | SnapPizzicato | Pizzicato | Latching | no | base |
| 3 | D#-1 | RandomPizzicato | Pizzicato | Latching | no | base |
| 4 | E-1 | Harmonic | Harmonic | Latching | no | base |
| 5 | F-1 | Mute | Mute | Latching | no | base |
| 6 | F#-1 | PalmMute | Mute | Latching | no | base |
| 7 | G-1 | Tremolo8th | Tremolo | Latching | no | base |
| 8 | G#-1 | Tremolo16th | Tremolo | Latching | no | base |
| 9 | A-1 | Tremolo32nd | Tremolo | Latching | no | base |
| 10 | A#-1 | Tremolo64th | Tremolo | Latching | no | base |
| 11 | B-1 | Trill | Tremolo | Latching | no | base |
| 12 | C0 | Legato | Picked | Modifier | yes | complementary |

Legato (MIDI 12) is a **modifier**: it is held between its Note On and Note Off and
layers over whichever timbre is active (it does not replace it). Every other keyswitch
**latches**: one Note On selects it, no Note Off, held until the next keyswitch.

## Host integration tiers

| Tier | How articulations reach Plectro | Hosts |
|:-----|:--------------------------------|:------|
| A. Auto discovery | Host queries the plugin over VST3 `IKeyswitchController` and builds its own mapping | Cubase, Dorico, MuseScore (fork) |
| B. Official file | Host imports a map file Plectro ships | Logic Pro (`.plist`), Cubase (`.expressionmap`), Dorico (`.doricolib`) |
| C. Manual MIDI | User places the keyswitch notes by hand | GarageBand, Ableton Live, simple AU/VST3 hosts |

## Per host compatibility

| Host | Format | Discovery | Articulations | Legato | Official artifact |
|:-----|:-------|:----------|:--------------|:-------|:------------------|
| Cubase | VST3 | Yes (`IKeyswitchController`) | Yes | Manual note | `generated/cubase/Plectro.expressionmap` (optional) |
| Dorico | VST3 | Yes (`IKeyswitchController`) | Yes | Playing technique | `generated/dorico/Plectro.doricolib` or import the Cubase map |
| Logic Pro | Audio Unit | No | Yes (Articulation Set) | Manual, see note | `generated/logic/Plectro.plist` |
| GarageBand (macOS) | Audio Unit | No | Manual MIDI notes | Manual note | none (manual) |
| GarageBand (iOS) | AUv3 | n/a | n/a | n/a | not supported yet (future work) |
| MuseScore (fork) | VST3 | Yes (`IKeyswitchController`) | Yes | Slur span | none needed |
| Studio One | VST3 | Yes (Sound Variations read KS) | Yes | Manual note | none (manual) |
| REAPER | VST3/AU | No | Manual / Reaticulate | Manual note | none (manual) |
| Ableton Live | VST3/AU | No | Manual MIDI notes | Manual note | none (manual) |

## Installing the map files

**Logic Pro** (`generated/logic/Plectro.plist`): copy to
`~/Music/Audio Music Apps/Articulation Settings/`, then on the Plectro track open the
Track inspector and pick **Plectro** under Articulation Set. Selecting an articulation
sends its keyswitch note. Logic labels MIDI 0 as C-2.

**Cubase** (`generated/cubase/Plectro.expressionmap`): copy to
`~/Documents/Steinberg/Cubase/Expression Maps/`, then in the Inspector Expression Map
field choose **Load Expression Map**. Cubase can also build the map itself from the
plugin's advertised keyswitches, so this file is optional.

**Dorico** (`generated/dorico/Plectro.doricolib`): Library > Library Manager >
import, or double click the file. Alternatively import the Cubase expression map from
Play > Expression Maps. Dorico can also read the plugin's advertised keyswitches.

**GarageBand / Ableton / REAPER / others**: no articulation file. Place the keyswitch
notes from the map above on the track just before the notes they affect. In pianoroll
hosts, draw the keyswitch note (for example MIDI 7 for Tremolo8th) a little before the
passage; it will not sound, it only switches the articulation.

## Legato limitations by host

Legato is a held modifier (Note On at the slur start, Note Off at the end). Over VST3
`IKeyswitchController` it is advertised as a held key-range (`kKeyRangeTypeID`) while the
other techniques are press-before switches (`kNoteOnKeyswitchTypeID`), so an auto
discovery host holds it across the passage. Auto discovery hosts and the MuseScore fork
drive it from the score's slurs. In file based
hosts you must hold the Legato keyswitch (MIDI 12) down across the slurred notes and
release it at the end. Logic Articulation Sets model momentary switches awkwardly, so
legato in Logic is best done by holding the manual keyswitch note. Slur aware tremolo
is a Pro edition feature; the free edition always uses the pick plus tremolo attack.

## Manual smoke test

On any host, play a sustained note while placing each keyswitch just before it and
confirm the timbre changes:

- **Standard** (MIDI 0): normal picked tone.
- **Pizzicato** (MIDI 1): short plucked tone.
- **Tremolo8th** (MIDI 7): one sustained tremolo, not a machine gun of repeats.
- **Trill** (MIDI 11): a sustained tremolo roll on the written pitch.
- **Legato** (MIDI 12): hold it across two slurred notes; the second connects without a
  fresh attack (Pro edition).

## GarageBand for iOS

The current Audio Unit is a macOS AU (v2/v3 for macOS). GarageBand for iOS requires an
**iOS AUv3** build, a different target and packaging. It is not included here and is
tracked as future work. macOS AU support does not imply iOS support.
