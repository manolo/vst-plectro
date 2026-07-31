#include "PresetMapping.h"

#include <cctype>

namespace plectro {

namespace {
std::string toLower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool nameHas(const std::string& name, const char* keyword)
{
    return toLower(name).find(keyword) != std::string::npos;
}
} // namespace

std::array<int, kNumArticulations> articulationPresetMap(const std::vector<SoundFontPreset>& bank)
{
    // Base / picked preset: the lowest-numbered preset whose name carries none of the articulation
    // suffixes. If every preset is suffixed, fall back to the lowest preset number, then to 0.
    int base = -1;
    for (const auto& p : bank)
    {
        const std::string n = toLower(p.name);
        const bool suffixed = n.find("trem") != std::string::npos
                              || n.find("p+t") != std::string::npos
                              || n.find("pizz") != std::string::npos
                              || n.find("harm") != std::string::npos
                              || n.find("mute") != std::string::npos;
        if (!suffixed && (base < 0 || p.preset < base))
            base = p.preset;
    }
    if (base < 0)
        for (const auto& p : bank)
            if (base < 0 || p.preset < base)
                base = p.preset;
    if (base < 0)
        base = 0;

    // First preset whose name matches the keyword, else the base preset.
    auto findByName = [&](const char* keyword) {
        for (const auto& p : bank)
            if (nameHas(p.name, keyword))
                return p.preset;
        return base;
    };

    std::array<int, kNumArticulations> map{};
    map[static_cast<int>(Articulation::Auto)] = base;
    map[static_cast<int>(Articulation::Picked)] = base;
    map[static_cast<int>(Articulation::Tremolo)] = findByName("trem");
    map[static_cast<int>(Articulation::Pizzicato)] = findByName("pizz");
    map[static_cast<int>(Articulation::Harmonic)] = findByName("harm");
    map[static_cast<int>(Articulation::Mute)] = findByName("mute");
    return map;
}

} // namespace plectro
