#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "Edition.h"

using namespace plectro;

// plectro_core (and the test binary) are compiled without PLECTRO_PRO,
// so kEdition must be the free edition here.
TEST_CASE("free edition is the default build")
{
    REQUIRE(std::string_view(kEdition.productName) == "Plectro");
    REQUIRE(kEdition.humanization == false);
    REQUIRE(kEdition.allowCustomSf2 == false);
    REQUIRE(kEdition.legatoTremolo == false); // slur-aware tremolo is Pro only
    REQUIRE(std::string_view(kEdition.bundledSf2) == "Plectro.sf2");
}
