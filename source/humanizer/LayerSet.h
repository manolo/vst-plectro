// Ensemble ("All <family>") layer planning. Pure and JUCE-free so it can be unit tested on its
// own. Given a family's SF2 banks, it decides which layers to render: a central anchor kept in
// tune and on tempo, plus humanized copies (one per family bank, each with its own seed) that
// decorrelate into a section. See specs/all-families-design.md.
#pragma once

#include <vector>

namespace plectro {

// Sentinel values of the instrument_bank parameter that select a whole family instead of a
// single bank. Real instrument banks are 0..29, so these stay clear of them.
inline constexpr int kAllBandurria = 100;
inline constexpr int kAllLaud      = 101;
inline constexpr int kAllMandolina = 102;

enum class Family { None, Bandurria, Laud, Mandolina };

// The central anchor is trimmed (about -3 dB) so it blends into the section instead of leading it,
// while staying in tune and on tempo; the humanized copies sit quieter still as a halo around it.
inline constexpr float kEnsembleCentralGain   = 0.7f;
inline constexpr float kEnsembleHumanizedGain = 0.5f;

// Stereo placement of the ensemble. The layers are spread across the panorama (pan -1 = hard left,
// +1 = hard right) up to this width, so the section reads as several players sitting side by side
// rather than one point source. Single instruments stay centred.
inline constexpr float kEnsemblePanSpread = 0.8f;

// SF2 bank layout: bandurria 0..9, laud 10..19, mandolina 20..29.
Family familyOfBank(int bank);          // real bank -> its family (None if out of range)
Family familyForSelection(int bank);    // sentinel -> its family; real bank -> familyOfBank
bool   isAllSelection(int bank);        // true only for the sentinels above

struct BankRange { int lo = -1; int hi = -1; };
BankRange rangeOfFamily(Family f);      // inclusive bank range; None -> {-1,-1}

// One rendered layer of an instrument selection.
struct LayerSpec
{
    int   bank = 0;
    bool  neutralize = false; // force per note humanization off (the central anchor)
    int   seedOffset = 0;     // added to instanceSeed so each humanized layer decorrelates
    float gainMul = 1.0f;     // per layer RELATIVE gain; the central anchor is trimmed ~-3 dB
    float pan = 0.0f;         // stereo placement: -1 hard left, 0 centre, +1 hard right
};

// Plan the layers for a selection.
//   isAll == false: single instrument. familyBanks holds the one selected bank; the result is a
//                   single plain layer (humanization follows the global toggle in the caller).
//   isAll == true : familyBanks holds the family's banks ascending, familyBanks[0] is the central
//                   instrument. The result is: the central anchor (neutralized, trimmed ~-3 dB so
//                   it blends rather than leads) and one humanized copy per OTHER bank, each with a
//                   distinct seed offset and quieter still. Count = familyBanks.size(). The layers
//                   are also spread across the panorama via `pan` (see kEnsemblePanSpread).
//
// The global 1/sqrt(N) section attenuation is NOT included here (the caller applies it to the
// master gain); gainMul only carries the per layer relative trim.
std::vector<LayerSpec> buildLayerSet(const std::vector<int>& familyBanks, bool isAll);

} // namespace plectro
