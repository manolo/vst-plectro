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

// The central anchor carries full weight; the humanized copies sit quieter around it as a halo,
// so the section stays anchored to the in tune, on tempo central instrument.
inline constexpr float kEnsembleHumanizedGain = 0.5f;

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
    float gainMul = 1.0f;     // per layer RELATIVE gain; the central anchor carries about -1 dB
};

// Plan the layers for a selection.
//   isAll == false: single instrument. familyBanks holds the one selected bank; the result is a
//                   single plain layer (humanization follows the global toggle in the caller).
//   isAll == true : familyBanks holds the family's banks ascending, familyBanks[0] is the central
//                   instrument. The result is: the central anchor (neutralized, about -1 dB), a
//                   humanized copy of the central, and one humanized copy per remaining bank; each
//                   humanized layer gets a distinct seed offset. Count = familyBanks.size() + 1.
//
// The global 1/sqrt(N) section attenuation is NOT included here (the caller applies it to the
// master gain); gainMul only carries the central anchor's relative trim.
std::vector<LayerSpec> buildLayerSet(const std::vector<int>& familyBanks, bool isAll);

} // namespace plectro
