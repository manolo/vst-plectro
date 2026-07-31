#include "EditorSupport.h"
#include "Edition.h"
#include "humanizer/LayerSet.h"

#include <algorithm>

namespace plectro {

void populateInstrumentBox(juce::ComboBox& box, const std::vector<SoundFontEngine::PresetInfo>& presets)
{
    box.clear(juce::dontSendNotification);

    // Instrument entries are preset 0; sort by bank so each family is contiguous.
    std::vector<SoundFontEngine::PresetInfo> instr;
    for (const auto& pr : presets)
        if (pr.preset == 0)
            instr.push_back(pr);
    std::sort(instr.begin(), instr.end(),
              [](const SoundFontEngine::PresetInfo& a, const SoundFontEngine::PresetInfo& b) { return a.bank < b.bank; });

    // Family by bank: 0-9 bandurria, 10-19 laud, 20-29 mandolina.
    auto familyOf = [](int bank) { return bank <= 9 ? 0 : bank <= 19 ? 1 : bank <= 29 ? 2 : 3; };
    const char* prefix[3] = { "Band: ", "Laud: ", "Mand: " };
    const int allBank[3] = { kAllBandurria, kAllLaud, kAllMandolina };

    for (std::size_t i = 0; i < instr.size(); ++i)
    {
        const int fam = familyOf(instr[i].bank);
        if (fam > 2)
            continue;
        // Prefix with the family and drop the SF2 name's redundant leading letter ("B PSaezLin").
        juce::String name = instr[i].name;
        if (name.length() > 2 && name[1] == ' ')
            name = name.substring(2);
        box.addItem(juce::String(prefix[fam]) + name, instr[i].bank + 1); // itemId cannot be 0

        // After the last preset of a family, add its "All" ensemble, then a separator.
        const bool lastOfFamily = (i + 1 >= instr.size()) || (familyOf(instr[i + 1].bank) != fam);
        if (lastOfFamily)
        {
            box.addItem(juce::String(prefix[fam]) + "Ensemble", allBank[fam] + 1);
            if (i + 1 < instr.size())
                box.addSeparator();
        }
    }
}

juce::String instrumentNameForBank(int bank)
{
    if (bank == kAllBandurria || (bank >= 0 && bank <= 9))   return "Bandurria";
    if (bank == kAllLaud || (bank >= 10 && bank <= 19))      return "Laud";
    if (bank == kAllMandolina || (bank >= 20 && bank <= 29)) return "Mandolina";
    return juce::String::fromUTF8(kEdition.productName);
}

} // namespace plectro
