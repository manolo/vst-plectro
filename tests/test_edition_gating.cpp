#include <catch2/catch_test_macros.hpp>

#include "humanizer/Types.h"

using namespace plectro;

TEST_CASE("neutralizePerNoteVariation zeroes the per note humanization")
{
    HumanizerParams p;
    p.jitterMs = 15.0;
    p.breathingDepthMs = 15.0;
    p.variationDepth = 0.1;
    p.lengthVariation = 0.05;
    p.legatoOverlapMs = 60.0;
    p.detuneCents = 4.0;

    neutralizePerNoteVariation(p);

    REQUIRE(p.jitterMs == 0.0);
    REQUIRE(p.breathingDepthMs == 0.0);
    REQUIRE(p.variationDepth == 0.0);
    REQUIRE(p.lengthVariation == 0.0);
    REQUIRE(p.legatoOverlapMs == 0.0);
    REQUIRE(p.detuneCents == 0.0);
}
