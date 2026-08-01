// Per note dynamic leveller. The host maps each score dynamic to a note velocity over a wide range
// (roughly ppp = 2 .. fff = 127), so pp is nearly inaudible and ff is very loud. This pulls every
// note's loudness toward the loudness of a musical centre (mp) by a 0..1 amount: below the centre a
// note is lifted, above it a note is tamed, at the centre nothing changes. At amount 1 every note
// lands at the centre loudness (nearly flat dynamics). Interpolation is in dB so it tracks perceived
// loudness. JUCE free so it is shared and unit testable.
#pragma once

#include <algorithm>
#include <cmath>

namespace plectro {

// The velocity treated as the neutral centre (mp): notes here are left untouched.
inline constexpr int kLevelerCenterVelocity = 55;

// The gain a note gets from its velocity before levelling (the plain velocity-to-gain curve).
inline double dynamicBaseGain(int velocity, double velocityCurve)
{
    const double v = std::clamp(velocity, 0, 127) / 127.0;
    return std::pow(v, std::max(0.01, velocityCurve));
}

// The gain change the leveller applies for a note, in dB. Signed: positive lifts a soft note toward
// the centre, negative tames a loud one. Zero at the centre velocity and at amount 0.
inline double levelerGainChangeDb(int velocity, double velocityCurve, double amount)
{
    const double a = std::clamp(amount, 0.0, 1.0);
    const double baseDb = 20.0 * std::log10(std::max(1.0e-4, dynamicBaseGain(velocity, velocityCurve)));
    const double centerDb = 20.0 * std::log10(std::max(1.0e-4, dynamicBaseGain(kLevelerCenterVelocity, velocityCurve)));
    return a * (centerDb - baseDb);
}

// The levelled linear gain for a note (the base gain with the levelling change applied).
inline double leveledGain(int velocity, double velocityCurve, double amount)
{
    const double base = dynamicBaseGain(velocity, velocityCurve);
    return base * std::pow(10.0, levelerGainChangeDb(velocity, velocityCurve, amount) / 20.0);
}

} // namespace plectro
