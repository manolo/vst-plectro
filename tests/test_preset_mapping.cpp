#include <catch2/catch_test_macros.hpp>

#include "humanizer/PresetMapping.h"

using namespace plectro;

static int presetFor(const std::array<int, kNumArticulations>& m, Articulation a)
{
    return m[static_cast<int>(a)];
}

TEST_CASE("Articulations route to SF2 presets by name, matching the Pro layout", "[presetmap]")
{
    // The SF2 bank layout: 0 base, 1 Trem, 2 P+T, 3 Mute, 4 Pizz, 5 Harm.
    const std::vector<SoundFontPreset> bank = {
        {0, "B PSaezLin"}, {1, "B PSaezLin Trem"}, {2, "B PSaezLin P+T"},
        {3, "B PSaezLin Mute"}, {4, "B PSaezLin Pizz"}, {5, "B PSaezLin Harm"},
    };
    const auto m = articulationPresetMap(bank);

    REQUIRE(presetFor(m, Articulation::Picked) == 0);     // base sample, no suffix
    REQUIRE(presetFor(m, Articulation::Auto) == 0);       // Auto follows picked
    REQUIRE(presetFor(m, Articulation::Tremolo) == 1);    // "Trem", not the "P+T" at 2
    REQUIRE(presetFor(m, Articulation::Mute) == 3);
    REQUIRE(presetFor(m, Articulation::Pizzicato) == 4);  // the bug: used to select 2 (P+T)
    REQUIRE(presetFor(m, Articulation::Harmonic) == 5);
}

TEST_CASE("Missing articulations fall back to the picked/base preset", "[presetmap]")
{
    // Plectro free bank layout: only base, Trem and P+T. No pizz/harm/mute samples.
    const std::vector<SoundFontPreset> bank = {
        {0, "B PSaezLin"}, {1, "B PSaezLin Trem"}, {2, "B PSaezLin P+T"},
    };
    const auto m = articulationPresetMap(bank);

    REQUIRE(presetFor(m, Articulation::Picked) == 0);
    REQUIRE(presetFor(m, Articulation::Tremolo) == 1);
    REQUIRE(presetFor(m, Articulation::Pizzicato) == 0); // not present -> base, stays audible
    REQUIRE(presetFor(m, Articulation::Harmonic) == 0);
    REQUIRE(presetFor(m, Articulation::Mute) == 0);
}
