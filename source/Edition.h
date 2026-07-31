#pragma once

namespace plectro {

// One compile time descriptor selects which edition this build is. Selected by the
// PLECTRO_PRO macro (set per target in CMake). This is the single source of truth for
// every edition difference.
struct Edition
{
    const char* productName;    // shown in the host and the editor title
    bool        humanization;   // false in free: no expressive processing
    bool        allowCustomSf2; // false in free: no Load SF2 button
    bool        legatoTremolo;  // false in free: a slur never switches a tremolo to Trem (stays P+T)
    const char* bundledSf2;     // file name of the SF2 shipped in the bundle
};

#if defined(PLECTRO_PRO) && PLECTRO_PRO
#else
inline constexpr Edition kEdition { "Plectro", false, false, false, "Plectro.sf2" };
#endif

} // namespace plectro
