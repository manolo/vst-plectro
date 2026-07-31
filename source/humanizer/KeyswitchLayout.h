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

struct KeyswitchDef
{
    int note;                  // keyswitch MIDI note (in the reserved low zone)
    const char* name;          // exact MuseScore mpe::ArticulationType name
    Articulation articulation; // internal articulation this keyswitch selects
};

inline constexpr std::array<KeyswitchDef, 12> kKeyswitchLayout = { {
    { 0, "Standard", Articulation::Picked },
    // Pizzicato family: distinct keyswitches, all rendered with the one pizzicato sound.
    { 1, "Pizzicato", Articulation::Pizzicato },
    { 2, "SnapPizzicato", Articulation::Pizzicato },
    { 3, "RandomPizzicato", Articulation::Pizzicato },
    { 4, "Harmonic", Articulation::Harmonic },
    // Mute family: distinct keyswitches, all rendered with the one mute sound.
    { 5, "Mute", Articulation::Mute },
    { 6, "PalmMute", Articulation::Mute },
    // Tremolo subdivisions: distinct keyswitches, all rendered with the one tremolo.
    { 7, "Tremolo8th", Articulation::Tremolo },
    { 8, "Tremolo16th", Articulation::Tremolo },
    { 9, "Tremolo32nd", Articulation::Tremolo },
    { 10, "Tremolo64th", Articulation::Tremolo },
    // Trill: rendered as a single sustained tremolo on the main note (the processor drops the
    // alternating upper note), so it uses the same tremolo sound.
    { 11, "Trill", Articulation::Tremolo },
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
            return std::string_view(k.name) == "Trill";
    return false;
}

} // namespace plectro
