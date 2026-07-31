#include "LayerSet.h"

namespace plectro {

Family familyOfBank(int bank)
{
    if (bank >= 0 && bank <= 9)   return Family::Bandurria;
    if (bank >= 10 && bank <= 19) return Family::Laud;
    if (bank >= 20 && bank <= 29) return Family::Mandolina;
    return Family::None;
}

bool isAllSelection(int bank)
{
    return bank == kAllBandurria || bank == kAllLaud || bank == kAllMandolina;
}

Family familyForSelection(int bank)
{
    switch (bank)
    {
        case kAllBandurria: return Family::Bandurria;
        case kAllLaud:      return Family::Laud;
        case kAllMandolina: return Family::Mandolina;
        default:            return familyOfBank(bank);
    }
}

BankRange rangeOfFamily(Family f)
{
    switch (f)
    {
        case Family::Bandurria: return { 0, 9 };
        case Family::Laud:      return { 10, 19 };
        case Family::Mandolina: return { 20, 29 };
        case Family::None:      break;
    }
    return {};
}

std::vector<LayerSpec> buildLayerSet(const std::vector<int>& familyBanks, bool isAll)
{
    std::vector<LayerSpec> layers;
    if (familyBanks.empty())
        return layers;

    if (!isAll)
    {
        // Single instrument: one plain layer. Humanization follows the global toggle upstream.
        layers.push_back({ familyBanks.front(), /*neutralize=*/false, /*seedOffset=*/0, /*gainMul=*/1.0f });
        return layers;
    }

    // Ensemble. The central anchor is the lowest bank, kept in tune and on tempo, at full weight so
    // it leads the section. Then one humanized copy per OTHER family bank (the central is not
    // duplicated), each with a distinct seed offset and sitting quieter around the anchor. A single
    // bank family therefore renders as just the central.
    layers.push_back({ familyBanks.front(), /*neutralize=*/true, /*seedOffset=*/0, /*gainMul=*/1.0f });

    for (std::size_t i = 1; i < familyBanks.size(); ++i)
        layers.push_back({ familyBanks[i], /*neutralize=*/false,
                           /*seedOffset=*/static_cast<int>(i), kEnsembleHumanizedGain });

    return layers;
}

} // namespace plectro
