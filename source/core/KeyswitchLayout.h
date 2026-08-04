// GENERATED FILE - do not edit by hand.
// Source of truth: articulations/plectro-articulations.json
// Regenerate with: python3 tools/generate_articulation_maps.py
//
// Single source of truth for Plectro's keyswitch layout: the keyswitch MIDI note, the exact
// MuseScore articulation name advertised via VST3 IKeyswitchController (so the host matches it
// without heuristics), and the internal articulation it selects. Both the IKeyswitchController
// advertiser (KeyswitchSupport.h) and the note decoder (articulationForKeyswitch) derive from this
// table, so titles and note decoding cannot drift apart. JUCE-free so it can be unit tested.
//
// The four tremolo subdivisions are advertised as distinct keyswitches because MuseScore notates
// them distinctly; Plectro collapses them to the same internal tremolo.
#pragma once

#include "Types.h" // Articulation

#include <array>
#include <string_view>

namespace plectro {

// How a keyswitch is delivered and held (see articulations/plectro-articulations.json):
//   Latching  - a single Note On selects it and it holds until another keyswitch changes it.
//   Momentary - active only while held (Note On..Note Off). Reserved; none ship today.
//   Span      - held across a range of notes. Reserved for future range techniques.
//   Modifier  - layers over the current timbre instead of replacing it (Legato).
enum class KeyswitchBehavior { Latching, Momentary, Span, Modifier };

// A base timbre versus a complementary modifier layered over one.
enum class ArticulationCategory { Base, Complementary };

struct KeyswitchDef
{
    int note;                      // keyswitch MIDI note (in the reserved low zone)
    const char* id;                // stable internal identifier
    const char* name;              // exact MuseScore mpe::ArticulationType name (host matches this)
    const char* shortTitle;        // abbreviated label for host displays
    Articulation articulation;     // internal articulation this keyswitch selects
    KeyswitchBehavior behavior;    // how the keyswitch is held
    bool needsNoteOff;             // host must send a Note Off (modifiers/spans)
    ArticulationCategory category; // base timbre vs complementary modifier
};

inline constexpr std::array<KeyswitchDef, 13> kKeyswitchLayout = { {
    { 0, "standard", "Standard", "Std", Articulation::Picked, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 1, "pizzicato", "Pizzicato", "Pizz", Articulation::Pizzicato, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 2, "snap_pizzicato", "SnapPizzicato", "Snap", Articulation::Pizzicato, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 3, "random_pizzicato", "RandomPizzicato", "RndPz", Articulation::Pizzicato, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 4, "harmonic", "Harmonic", "Harm", Articulation::Harmonic, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 5, "mute", "Mute", "Mute", Articulation::Mute, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 6, "palm_mute", "PalmMute", "PMute", Articulation::Mute, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 7, "tremolo_8th", "Tremolo8th", "Trem8", Articulation::Tremolo, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 8, "tremolo_16th", "Tremolo16th", "Trm16", Articulation::Tremolo, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 9, "tremolo_32nd", "Tremolo32nd", "Trm32", Articulation::Tremolo, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 10, "tremolo_64th", "Tremolo64th", "Trm64", Articulation::Tremolo, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 11, "trill", "Trill", "Trill", Articulation::Tremolo, KeyswitchBehavior::Latching, false, ArticulationCategory::Base },
    { 12, "legato", "Legato", "Leg", Articulation::Picked, KeyswitchBehavior::Modifier, true, ArticulationCategory::Complementary },
} };

// Decode a keyswitch-zone MIDI note to the articulation it selects. A note outside the layout
// falls back to Picked (normal), matching "unmapped keyswitch = back to normal".
inline Articulation articulationForKeyswitchNote(int note)
{
    for (const auto& k : kKeyswitchLayout)
        if (k.note == note)
            return k.articulation;
    return Articulation::Picked;
}

// Whether the keyswitch at this note is the Trill keyswitch. Trills are rendered as one sustained
// tremolo on the main note, so the processor detects this note to drop the alternating upper note.
inline bool keyswitchNoteIsTrill(int note)
{
    for (const auto& k : kKeyswitchLayout)
        if (k.note == note)
            return std::string_view(k.id) == "trill";
    return false;
}

// Whether the keyswitch at this note is the Legato modifier. The processor uses it to arm the
// per-channel legato latch (Trem vs P+T for tremolo), independently of the articulation latch.
inline bool keyswitchNoteIsLegato(int note)
{
    for (const auto& k : kKeyswitchLayout)
        if (k.note == note)
            return std::string_view(k.id) == "legato";
    return false;
}

} // namespace plectro
