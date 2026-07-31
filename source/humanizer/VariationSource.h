// The per note expressive deviations, the actual humanization math (timing jitter, breathing,
// dynamics, detune, pitch drift, length). It is split behind this interface so the algorithms live
// only in the Pro build: the free build links a neutral implementation and never ships the
// humanization source. Implementations are stateless; exactly one is compiled into each build and
// returned by defaultVariationSource() (selected at link time, no preprocessor). JUCE free.
// See specs/distribution-plan.md (Phase 3a).
#pragma once

#include "Types.h"

#include <cstdint>

namespace plectro {

struct VariationSource
{
    virtual ~VariationSource() = default;

    // Per note onset timing offset in samples (0 = dead on the beat).
    virtual double timingJitter(const HumanizerParams& p, std::int64_t startOn, int key) const = 0;
    // Slow phrase level timing drift in samples at time t (0 = none).
    virtual double breathing(const HumanizerParams& p, std::int64_t t) const = 0;
    // Per note gain multiplier around 1.0 (1.0 = no dynamic variation).
    virtual float gainVariation(const HumanizerParams& p, std::int64_t startOn, int key) const = 0;
    // Map a MIDI velocity to a 0..1 linear gain, applying the given per note variation multiplier.
    virtual float applyGain(const HumanizerParams& p, int velocity, float variation) const = 0;
    // Per note pitch offset in cents (0 = in tune).
    virtual float detune(const HumanizerParams& p, std::int64_t startOn, int key) const = 0;
    // Slow pitch wander in cents at time t, for sustained notes (0 = none).
    virtual double pitchDrift(const HumanizerParams& p, std::int64_t t) const = 0;
    // Adjust a note-off sample to vary the note length (returns rawOff unchanged for no variation).
    virtual std::int64_t lengthOffset(const HumanizerParams& p, std::int64_t startOn,
                                      std::int64_t rawOff, int key) const = 0;
};

// The implementation for this build: neutral in free, humanized in Pro. Defined by exactly one of
// the two edition implementations (the free neutral one, or the Pro build's humanized one), never
// both.
const VariationSource& defaultVariationSource();

} // namespace plectro
