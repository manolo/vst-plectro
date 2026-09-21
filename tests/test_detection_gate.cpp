#include <catch2/catch_test_macros.hpp>

#include "core/DetectionGate.h"

using namespace plectro;

TEST_CASE("Auto detection is off when the master tremolo toggle is off", "[detection]")
{
    REQUIRE_FALSE(shouldAutoDetect(/*tremoloEnabled*/ false, /*keyswitchSeen*/ false,
                                   /*hostQueried*/ false, /*notes*/ 100));
}

TEST_CASE("An actual keyswitch turns auto detection off", "[detection]")
{
    // A host that sends keyswitches drives articulations explicitly; the detector stands down.
    REQUIRE_FALSE(shouldAutoDetect(true, /*keyswitchSeen*/ true, false, 0));
    REQUIRE_FALSE(shouldAutoDetect(true, /*keyswitchSeen*/ true, true, 100));
}

TEST_CASE("A plain MIDI host that never queries keeps auto detection on", "[detection]")
{
    REQUIRE(shouldAutoDetect(true, false, /*hostQueried*/ false, 0));
    REQUIRE(shouldAutoDetect(true, false, /*hostQueried*/ false, 50));
}

TEST_CASE("A query suppresses detection at first but never forever", "[detection]")
{
    // Right after the query (MuseScore-like), suppress so ornament expansions do not misfire.
    REQUIRE_FALSE(shouldAutoDetect(true, false, /*hostQueried*/ true, /*notes*/ 0));
    REQUIRE_FALSE(shouldAutoDetect(true, false, true, kQueryGraceNotes - 1));

    // A query-only host (queried, but the user placed no keyswitch) gets detection back once enough
    // musical notes have played: the bare query does not disable it indefinitely.
    REQUIRE(shouldAutoDetect(true, false, true, kQueryGraceNotes));
    REQUIRE(shouldAutoDetect(true, false, true, kQueryGraceNotes + 100));
}

TEST_CASE("A late keyswitch still wins over the grace re-enable", "[detection]")
{
    // Even past the grace window, seeing a keyswitch takes precedence and keeps detection off.
    REQUIRE_FALSE(shouldAutoDetect(true, /*keyswitchSeen*/ true, true, kQueryGraceNotes + 100));
}
