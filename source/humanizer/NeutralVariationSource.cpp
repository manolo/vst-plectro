// Free edition implementation of VariationSource: no humanization at all. Notes play dead on the
// beat, in tune, at a plain velocity mapped gain. This is the only variation source compiled into
// the free build, so the humanization algorithms never ship in the public repo.
#include "VariationSource.h"

#include <algorithm>

namespace plectro {

namespace {
struct NeutralVariationSource : VariationSource
{
    double timingJitter(const HumanizerParams&, std::int64_t, int) const override { return 0.0; }
    double breathing(const HumanizerParams&, std::int64_t) const override { return 0.0; }
    float gainVariation(const HumanizerParams&, std::int64_t, int) const override { return 1.0f; }
    float applyGain(const HumanizerParams&, int velocity, float) const override
    {
        return static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
    }
    float detune(const HumanizerParams&, std::int64_t, int) const override { return 0.0f; }
    double pitchDrift(const HumanizerParams&, std::int64_t) const override { return 0.0; }
    std::int64_t lengthOffset(const HumanizerParams&, std::int64_t, std::int64_t rawOff, int) const override
    {
        return rawOff;
    }
};
} // namespace

const VariationSource& defaultVariationSource()
{
    static const NeutralVariationSource instance;
    return instance;
}

} // namespace plectro
