#include <catch2/catch_test_macros.hpp>

#include "core/KeyswitchLayout.h"

#include <set>
#include <string>
#include <string_view>

using namespace plectro;

static Articulation artForName(const char* name)
{
    for (const auto& k : kKeyswitchLayout)
        if (std::string(k.name) == name)
            return k.articulation;
    return Articulation::Auto; // sentinel: name not present in the layout
}

TEST_CASE("Keyswitch notes decode to the articulation declared in the layout", "[keyswitch][layout]")
{
    for (const auto& k : kKeyswitchLayout)
        REQUIRE(articulationForKeyswitchNote(k.note) == k.articulation);

    REQUIRE(articulationForKeyswitchNote(99) == Articulation::Picked); // outside the layout -> normal
}

TEST_CASE("Plugin exposes canonical names and collapses variants internally", "[keyswitch][layout]")
{
    // The host does pure 1:1 matching, so the plugin advertises every articulation it handles and
    // decides internally how to render each. No host side aliasing.
    REQUIRE(artForName("Standard") == Articulation::Picked);

    // Pizzicato family: distinct keyswitches, one internal pizzicato sound.
    REQUIRE(artForName("Pizzicato") == Articulation::Pizzicato);
    REQUIRE(artForName("SnapPizzicato") == Articulation::Pizzicato);
    REQUIRE(artForName("RandomPizzicato") == Articulation::Pizzicato);

    // Mute family (incl. palm mute): distinct keyswitches, one internal mute sound.
    REQUIRE(artForName("Mute") == Articulation::Mute);
    REQUIRE(artForName("PalmMute") == Articulation::Mute);

    REQUIRE(artForName("Harmonic") == Articulation::Harmonic);

    // Trill is advertised under its canonical name and rendered as one sustained tremolo.
    REQUIRE(artForName("Trill") == Articulation::Tremolo);
    REQUIRE(keyswitchNoteIsTrill(11));
    REQUIRE_FALSE(keyswitchNoteIsTrill(1));  // Pizzicato, not a trill

    // Tremolo subdivisions: distinct keyswitches, one internal tremolo.
    REQUIRE(artForName("Tremolo8th") == Articulation::Tremolo);
    REQUIRE(artForName("Tremolo16th") == Articulation::Tremolo);
    REQUIRE(artForName("Tremolo32nd") == Articulation::Tremolo);
    REQUIRE(artForName("Tremolo64th") == Articulation::Tremolo);

    // Every keyswitch has a distinct note.
    std::set<int> notes;
    for (const auto& k : kKeyswitchLayout)
        notes.insert(k.note);
    REQUIRE(notes.size() == kKeyswitchLayout.size());
}

TEST_CASE("Legato keyswitch is advertised as a modifier on its own note", "[keyswitch][layout]")
{
    // Exactly one entry titled "Legato", on note 12, distinct from every other note.
    int legatoNote = -1, count = 0;
    for (const auto& k : kKeyswitchLayout)
        if (std::string_view(k.name) == "Legato") { ++count; legatoNote = k.note; }
    REQUIRE(count == 1);
    REQUIRE(legatoNote == 12);

    REQUIRE(keyswitchNoteIsLegato(12));
    REQUIRE_FALSE(keyswitchNoteIsLegato(7));   // a tremolo keyswitch
    REQUIRE_FALSE(keyswitchNoteIsLegato(0));   // Standard
}

TEST_CASE("The layout covers the official map MIDI 0..12 with no gaps", "[keyswitch][layout]")
{
    REQUIRE(kKeyswitchLayout.size() == 13);

    // Every official keyswitch note 0..12 is present exactly once.
    std::set<int> notes;
    for (const auto& k : kKeyswitchLayout)
        notes.insert(k.note);
    for (int n = 0; n <= 12; ++n)
        REQUIRE(notes.count(n) == 1);

    REQUIRE(kKeyswitchLayout.front().note == 0);
    REQUIRE(kKeyswitchLayout.back().note == 12);
}

TEST_CASE("Every entry has a unique id and non-empty names", "[keyswitch][layout]")
{
    std::set<std::string> ids;
    for (const auto& k : kKeyswitchLayout)
    {
        REQUIRE(std::string_view(k.name).size() > 0);       // host matches on this
        REQUIRE(std::string_view(k.id).size() > 0);         // stable internal id
        REQUIRE(std::string_view(k.shortTitle).size() > 0); // advertised via IKeyswitchController
        ids.insert(k.id);
    }
    REQUIRE(ids.size() == kKeyswitchLayout.size());         // ids are unique
}

TEST_CASE("Behaviors follow the official map: latching bases plus a legato modifier", "[keyswitch][layout]")
{
    for (const auto& k : kKeyswitchLayout)
    {
        if (std::string_view(k.id) == "legato")
        {
            // Legato is the only modifier, the only entry that needs a note off, and complementary.
            REQUIRE(k.behavior == KeyswitchBehavior::Modifier);
            REQUIRE(k.needsNoteOff);
            REQUIRE(k.category == ArticulationCategory::Complementary);
        }
        else
        {
            // Every timbre latches, needs no note off, and is a base articulation.
            REQUIRE(k.behavior == KeyswitchBehavior::Latching);
            REQUIRE_FALSE(k.needsNoteOff);
            REQUIRE(k.category == ArticulationCategory::Base);
        }
    }
}
