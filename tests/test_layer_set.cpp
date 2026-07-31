#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "humanizer/LayerSet.h"

#include <cmath>
#include <set>

using namespace plectro;
using Catch::Matchers::WithinAbs;

TEST_CASE("Family classification by bank and selection", "[layerset]")
{
    REQUIRE(familyOfBank(0) == Family::Bandurria);
    REQUIRE(familyOfBank(9) == Family::Bandurria);
    REQUIRE(familyOfBank(10) == Family::Laud);
    REQUIRE(familyOfBank(19) == Family::Laud);
    REQUIRE(familyOfBank(20) == Family::Mandolina);
    REQUIRE(familyOfBank(29) == Family::Mandolina);
    REQUIRE(familyOfBank(30) == Family::None);
    REQUIRE(familyOfBank(-1) == Family::None);

    REQUIRE(isAllSelection(kAllBandurria));
    REQUIRE(isAllSelection(kAllLaud));
    REQUIRE(isAllSelection(kAllMandolina));
    REQUIRE_FALSE(isAllSelection(0));
    REQUIRE_FALSE(isAllSelection(29));

    REQUIRE(familyForSelection(kAllBandurria) == Family::Bandurria);
    REQUIRE(familyForSelection(kAllLaud) == Family::Laud);
    REQUIRE(familyForSelection(kAllMandolina) == Family::Mandolina);
    REQUIRE(familyForSelection(5) == Family::Bandurria);
    REQUIRE(familyForSelection(15) == Family::Laud);

    REQUIRE(rangeOfFamily(Family::Bandurria).lo == 0);
    REQUIRE(rangeOfFamily(Family::Bandurria).hi == 9);
    REQUIRE(rangeOfFamily(Family::Laud).lo == 10);
    REQUIRE(rangeOfFamily(Family::Mandolina).hi == 29);
    REQUIRE(rangeOfFamily(Family::None).lo == -1);
}

TEST_CASE("Single instrument is one plain layer", "[layerset]")
{
    const auto layers = buildLayerSet({ 5 }, /*isAll=*/false);
    REQUIRE(layers.size() == 1);
    REQUIRE(layers[0].bank == 5);
    REQUIRE_FALSE(layers[0].neutralize);
    REQUIRE(layers[0].seedOffset == 0);
    REQUIRE_THAT(layers[0].gainMul, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("All family layers: central anchor + humanized copies", "[layerset]")
{
    // Laud family present as {10, 19}: central(10) + hum(10) + hum(19) = 3 layers.
    const auto layers = buildLayerSet({ 10, 19 }, /*isAll=*/true);
    REQUIRE(layers.size() == 2); // central (10) + one humanized copy of the other bank (19)

    // Layer 0: central anchor, neutralized, full weight (the lead of the section).
    REQUIRE(layers[0].bank == 10);
    REQUIRE(layers[0].neutralize);
    REQUIRE_THAT(layers[0].gainMul, WithinAbs(1.0f, 1e-6f));

    // The remaining layers are humanized copies sitting quieter around the central anchor.
    for (std::size_t i = 1; i < layers.size(); ++i)
    {
        REQUIRE_FALSE(layers[i].neutralize);
        REQUIRE_THAT(layers[i].gainMul, WithinAbs(kEnsembleHumanizedGain, 1e-6f));
        REQUIRE(layers[i].gainMul < layers[0].gainMul); // the central anchor carries more weight
    }

    // The central bank is NOT duplicated; the other bank is the humanized copy.
    REQUIRE(layers[1].bank == 19);

    // Every humanized layer decorrelates: distinct, non zero seed offsets.
    std::set<int> humSeeds;
    for (std::size_t i = 1; i < layers.size(); ++i)
    {
        REQUIRE(layers[i].seedOffset != 0);
        humSeeds.insert(layers[i].seedOffset);
    }
    REQUIRE(humSeeds.size() == layers.size() - 1);
}

TEST_CASE("A single bank family renders as just the central (no duplication)", "[layerset]")
{
    // Only one bank present: the ensemble is just the central anchor, not duplicated.
    const auto layers = buildLayerSet({ 0 }, /*isAll=*/true);
    REQUIRE(layers.size() == 1);
    REQUIRE(layers[0].bank == 0);
    REQUIRE(layers[0].neutralize);
}

TEST_CASE("Ensemble with six banks yields six layers (central + humanized others)", "[layerset]")
{
    const auto layers = buildLayerSet({ 0, 1, 2, 3, 4, 9 }, /*isAll=*/true);
    REQUIRE(layers.size() == 6); // central + 5 humanized (the central bank is not duplicated)

    REQUIRE(layers[0].bank == 0);
    REQUIRE(layers[0].neutralize);
    const std::vector<int> humBanks = { layers[1].bank, layers[2].bank, layers[3].bank,
                                        layers[4].bank, layers[5].bank };
    REQUIRE(humBanks == std::vector<int>{ 1, 2, 3, 4, 9 });

    std::set<int> seeds;
    for (std::size_t i = 1; i < layers.size(); ++i)
        seeds.insert(layers[i].seedOffset);
    REQUIRE(seeds.size() == 5); // all humanized seeds distinct
}
