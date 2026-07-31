#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "Edition.h"

using namespace plectro;

// humanizer_core (and the test binary) are compiled without PLECTRO_PRO,
// so kEdition must be the free edition here.
TEST_CASE("free edition is the default build")
{
    REQUIRE(std::string_view(kEdition.productName) == "Plectro");
    REQUIRE(kEdition.humanization == false);
    REQUIRE(kEdition.allowCustomSf2 == false);
    REQUIRE(std::string_view(kEdition.bundledSf2) == "Plectro.sf2");
}
