// Maps articulations to SoundFont preset numbers by matching preset NAMES, so routing follows
// the loaded SF2's own layout instead of hard-coded indices. JUCE-free and engine-free so it can
// be unit tested on its own.
#pragma once

#include "Types.h" // Articulation, kNumArticulations

#include <array>
#include <string>
#include <vector>

namespace plectro {

// One preset of the SoundFont, within a single instrument bank.
struct SoundFontPreset
{
    int preset = 0;
    std::string name;
};

// Build the articulation -> preset-number map for one instrument bank by matching a distinctive
// substring of each preset's name (case-insensitive): "trem", "pizz", "harm", "mute". Picked (and
// Auto) map to the base preset: the one whose name carries none of those suffixes, else the lowest
// preset number. Any articulation not found in the bank (e.g. the free SoundFont has no pizzicato)
// falls back to the base/picked preset, so it stays audible instead of selecting a wrong sample.
std::array<int, kNumArticulations> articulationPresetMap(const std::vector<SoundFontPreset>& bankPresets);

// The "picked tremolo" (P+T) preset: a tremolo body that begins with a pick attack, used for a
// standalone tremolo or the first note of a slurred group. Falls back to the plain tremolo preset
// (then the base preset) when the SoundFont has no P+T sample.
int pickedTremoloPreset(const std::vector<SoundFontPreset>& bankPresets);

} // namespace plectro
