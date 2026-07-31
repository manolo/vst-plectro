// Real-time streaming scheduler. Unlike the batch Humanizer (which needs the whole note
// list up front), this consumes note events incrementally and emits voice commands as soon
// as an articulation can be decided, using only a look-ahead window. This is what the plugin
// uses on the audio thread so notes play with their real duration instead of collapsing.
//
// Contract: push() raw note events, then call advance(latestInputSample) once per block with
// the absolute sample index one-past-the-end of the input seen so far. Ready commands (times
// already shifted by the look-ahead) are appended to `out`. It is deterministic given the
// seeds in the params.
#pragma once

#include "Types.h"
#include "VariationSource.h"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace plectro {

class StreamingHumanizer
{
public:
    void reset();
    void setParams(const HumanizerParams& p) { params_ = p; }
    const HumanizerParams& params() const { return params_; }

    void push(const NoteEvent& e);
    void advance(std::int64_t latestInputSample, std::vector<VoiceCommand>& out);
    
    // Force close all active voices immediately and reset to Idle. Used when a keyswitch reset
    // is detected (e.g. Tremolo -> Picked) to allow new tremolo spans to retrigger properly.
    void forceCloseAll(std::int64_t atSample, std::vector<VoiceCommand>& out);

    // Current tremolo activity, for the editor's indicator LEDs: whether an explicit (keyswitch)
    // tremolo is sounding and whether the rhythmic detector is sustaining a tremolo.
    void tremoloActivity(bool& keyswitchTremolo, bool& detectorTremolo, bool& keyswitchTremoloLegato) const;

private:
    enum class Phase { Idle, Pick, Tremolo };

    struct KeyState
    {
        Phase phase = Phase::Idle;
        Articulation articulation = Articulation::Auto; // decided at the first onset of the note
        bool legato = false;    // captured at the span start; selects Trem (true) vs P+T (false)
        int pickVoice = 0;      // the immediate picked attack
        int tremVoice = 0;      // the sustained tremolo, started on the second stroke
        std::int64_t lastOn = 0;
        std::int64_t startOn = 0;
        std::int64_t lastInterval = 0; // spacing of the previous stroke, for adaptive tracking
        std::int64_t lastOffSeen = -1;
        std::int64_t pendingPickOff = -1; // note-off of the pick, held until classified
        double jitter = 0.0;    // per-note timing shift in samples
        float variation = 1.0f; // per-note gain multiplier
        float baseDetune = 0.0f; // per-note pitch offset in cents (instance offset + random)
    };

    // The humanization math, provided by the edition's implementation (neutral in free, humanized
    // in Pro). Selected at link time; see VariationSource.h.
    const VariationSource* variation_ = &defaultVariationSource();

    HumanizerParams params_;
    std::map<int, std::deque<NoteEvent>> perKey_;
    std::map<int, KeyState> state_;
    int nextVoiceId_ = 0;
};

} // namespace plectro
