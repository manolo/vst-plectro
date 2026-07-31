#include "StreamingScheduler.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace plectro {

void StreamingScheduler::reset()
{
    perKey_.clear();
    state_.clear();
    nextVoiceId_ = 0;
}

void StreamingScheduler::push(const NoteEvent& e)
{
    perKey_[e.key].push_back(e);
}

void StreamingScheduler::advance(std::int64_t latestInput, std::vector<VoiceCommand>& out)
{
    const std::int64_t W = std::max<std::int64_t>(1, params_.detectWindowSamples());
    const std::int64_t L = params_.lookaheadSamples;
    // Hold each note-off a little longer so fast notes overlap and decay instead of cutting.
    const std::int64_t overlap = static_cast<std::int64_t>(params_.legatoOverlapMs * 0.001 * params_.sampleRate);

    auto at = [&](std::int64_t inputSample, double jitter) {
        std::int64_t s = inputSample + static_cast<std::int64_t>(std::llround(jitter)) + L;
        return s < 0 ? std::int64_t{0} : s;
    };
    auto emitOn = [&](int preset, int voice, int key, std::int64_t when, float gain, float detune) {
        out.push_back({when, VoiceCommandType::NoteOn, voice, key, params_.engineVelocity, preset,
                       params_.bank, gain, detune});
    };
    auto emitOff = [&](int voice, int key, std::int64_t when) {
        out.push_back({when, VoiceCommandType::NoteOff, voice, key, 0, 0, 0, 0.0f, 0.0f});
    };
    auto emitPitch = [&](int voice, int key, std::int64_t when, float detune) {
        out.push_back({when, VoiceCommandType::SetPitch, voice, key, 0, 0, 0, 0.0f, detune});
    };

    // How wide a gap between strokes still counts as the same tremolo. For an explicit tremolo the
    // window does not apply: before the first interval is known we wait one beat (a tremolo always
    // subdivides the beat, so the next stroke is within a beat at any tempo), then we track the
    // observed interval. The Auto detector keeps its tight, capped window.
    const std::int64_t oneBeat = params_.beatSamples > 0
                                     ? params_.beatSamples
                                     : static_cast<std::int64_t>(0.5 * params_.sampleRate);
    auto tremTol = [&](const KeyState& s) -> std::int64_t {
        if (s.articulation == Articulation::Tremolo)
            return s.lastInterval > 0
                       ? std::max(W, static_cast<std::int64_t>(2.5 * static_cast<double>(s.lastInterval)))
                       : oneBeat;
        return std::min(static_cast<std::int64_t>(0.45 * params_.sampleRate),
                        std::max(W, static_cast<std::int64_t>(2.5 * static_cast<double>(s.lastInterval))));
    };

    std::set<int> keys;
    for (auto& kv : perKey_) keys.insert(kv.first);
    for (auto& kv : state_) keys.insert(kv.first);

    for (int key : keys)
    {
        auto& dq = perKey_[key];
        std::stable_sort(dq.begin(), dq.end(),
                         [](const NoteEvent& a, const NoteEvent& b) { return a.sample < b.sample; });
        auto& st = state_[key];

        bool progress = true;
        while (progress)
        {
            progress = false;

            if (st.phase == Phase::Idle)
            {
                if (dq.empty())
                    continue;
                const NoteEvent e = dq.front();
                if (!e.isNoteOn)
                {
                    dq.pop_front(); // stray note-off
                    progress = true;
                    continue;
                }
                // Play the picked attack immediately (this is also the first stroke of a
                // tremolo). No look-ahead delay on the onset, only the tiny fixed L.
                st.pickVoice = ++nextVoiceId_;
                // Tremolo off: a tremolo keyswitch plays picked. Otherwise the keyswitch is
                // honoured and the detector merges repeated notes (always Auto behaviour).
                st.articulation = (!params_.tremoloOn && e.articulation == Articulation::Tremolo)
                                      ? Articulation::Picked
                                      : e.articulation;
                st.legato = e.legato;
                st.startOn = e.sample;
                st.lastOn = e.sample;
                st.lastOffSeen = -1;
                st.pendingPickOff = -1;
                st.jitter = variation_->timingJitter(params_, e.sample, key) + variation_->breathing(params_, e.sample);
                st.variation = variation_->gainVariation(params_, e.sample, key);
                st.baseDetune = variation_->detune(params_, e.sample, key);
                // A tremolo onset picks its sample by legato: a slur continuation runs pure Trem
                // (presetFor(Tremolo)); a standalone tremolo (or the first note of a slur) attacks
                // with a pick (P+T, tremoloPickedPreset). Every other articulation uses its preset.
                const int onsetPreset =
                    (st.articulation == Articulation::Tremolo)
                        ? (st.legato ? params_.presetFor(Articulation::Tremolo) : params_.tremoloPickedPreset)
                        : params_.presetFor(st.articulation);
                emitOn(onsetPreset, st.pickVoice, key, at(e.sample, st.jitter),
                       variation_->applyGain(params_, e.velocity, st.variation),
                       st.baseDetune + static_cast<float>(variation_->pitchDrift(params_, e.sample)));
                if (st.articulation == Articulation::Tremolo)
                {
                    // Explicit tremolo: the host has told us this is a tremolo and the attack already
                    // plays the tremolo sample, so sustain it as one voice from the first stroke. No
                    // Pick promotion and no detector window: the strokes only follow the dynamic, and
                    // the span ends when they stop (Tremolo phase) or the host re-sends the keyswitch.
                    st.tremVoice = st.pickVoice;
                    st.lastInterval = 0; // unknown until the second stroke; the wait is one beat
                    st.phase = Phase::Tremolo;
                }
                else
                {
                    st.phase = Phase::Pick;
                }
                dq.pop_front();
                progress = true;
                continue;
            }

            if (st.phase == Phase::Pick)
            {
                if (!dq.empty())
                {
                    const NoteEvent e = dq.front();
                    if (!e.isNoteOn)
                    {
                        st.pendingPickOff = std::max(st.pendingPickOff, e.sample);
                        dq.pop_front();
                        progress = true;
                        continue;
                    }
                    // A second onset within the window sustains one tremolo voice instead of
                    // retriggering. This fires for Auto (the rhythmic detector, when enabled)
                    // and for an explicit Tremolo keyswitch: a notated stem tremolo arrives as
                    // rapid repeated notes, and we want one smooth sustained tremolo, not a
                    // machine-gun of separate notes. Other explicit articulations (pizzicato,
                    // harmonic, mute) never promote and stay one note per onset.
                    const bool sustainTremolo =
                        st.articulation == Articulation::Tremolo
                        || (st.articulation == Articulation::Auto && params_.enableDetection);
                    if (sustainTremolo && e.sample - st.lastOn <= W)
                    {
                        if (st.articulation == Articulation::Tremolo)
                        {
                            // Explicit tremolo: the attack already plays the tremolo sample, so
                            // sustain that single voice and only follow the dynamic. Adding a
                            // second tremolo voice would layer two offset copies of the recorded
                            // tremolo and phase against each other (double-tremolo artefact).
                            st.tremVoice = st.pickVoice;
                            const std::int64_t whenC = at(e.sample, st.jitter);
                            out.push_back({whenC, VoiceCommandType::SetGain, st.tremVoice, key,
                                           0, 0, 0, variation_->applyGain(params_, e.velocity, st.variation)});
                            emitPitch(st.tremVoice, key, whenC,
                                      st.baseDetune + static_cast<float>(variation_->pitchDrift(params_, e.sample)));
                        }
                        else
                        {
                            // Auto: the attack is a picked note; add a separate sustained
                            // tremolo voice over it so the pick and the tremolo body each use
                            // their own sample. Do NOT cut the initial pick: let it ring under
                            // the tremolo so there is no dip. It is released when the tremolo ends.
                            st.tremVoice = ++nextVoiceId_;
                            emitOn(params_.presetFor(Articulation::Tremolo), st.tremVoice, key, at(e.sample, st.jitter),
                                   variation_->applyGain(params_, e.velocity, st.variation),
                                   st.baseDetune + static_cast<float>(variation_->pitchDrift(params_, e.sample)));
                        }
                        st.lastInterval = e.sample - st.lastOn;
                        st.lastOn = e.sample;
                        st.lastOffSeen = -1;
                        st.phase = Phase::Tremolo;
                        dq.pop_front();
                        progress = true;
                        continue;
                    }
                    // Gap too big: it was a single picked note. Close it and reprocess this onset.
                    const std::int64_t off = st.pendingPickOff >= 0 ? st.pendingPickOff : st.lastOn;
                    emitOff(st.pickVoice, key, at(variation_->lengthOffset(params_, st.startOn, off, key) + overlap, st.jitter));
                    st.phase = Phase::Idle;
                    progress = true;
                    continue;
                }
                // No queued events: finalise a single picked note once its window has passed.
                if (st.pendingPickOff >= 0 && latestInput >= st.lastOn + W)
                {
                    emitOff(st.pickVoice, key,
                            at(variation_->lengthOffset(params_, st.startOn, st.pendingPickOff, key) + overlap, st.jitter));
                    st.phase = Phase::Idle;
                    progress = true;
                }
                continue;
            }

            // Tremolo.
            if (!dq.empty())
            {
                const NoteEvent e = dq.front();
                if (!e.isNoteOn)
                {
                    st.lastOffSeen = std::max(st.lastOffSeen, e.sample);
                    dq.pop_front();
                    progress = true;
                    continue;
                }
                // Adaptive tolerance (see tremTol): follow a ritardando by allowing the next stroke
                // within a multiple of the recent interval; an explicit tremolo is not gated by the
                // window, so slow-tempo strokes still continue the same voice.
                const std::int64_t gap = e.sample - st.lastOn;
                const std::int64_t tol = tremTol(st);
                if (gap <= tol)
                {
                    // Continuation stroke: the tremolo loop sustains; follow the dynamic and
                    // let the pitch drift slowly around the note's centre.
                    const std::int64_t whenC = at(e.sample, st.jitter);
                    out.push_back({whenC, VoiceCommandType::SetGain, st.tremVoice, key,
                                   0, 0, 0, variation_->applyGain(params_, e.velocity, st.variation)});
                    emitPitch(st.tremVoice, key, whenC, st.baseDetune + static_cast<float>(variation_->pitchDrift(params_, e.sample)));
                    st.lastInterval = gap;
                    st.lastOn = e.sample;
                    dq.pop_front();
                    progress = true;
                    continue;
                }
                const std::int64_t off = st.lastOffSeen >= 0 ? st.lastOffSeen : st.lastOn + W;
                emitOff(st.tremVoice, key, at(off + overlap, st.jitter));
                if (st.pickVoice != st.tremVoice)
                    emitOff(st.pickVoice, key, at(off + overlap, st.jitter)); // release the lingering pick
                st.phase = Phase::Idle;
                progress = true;
                continue;
            }
            const std::int64_t closeTol = tremTol(st);
            if (latestInput >= st.lastOn + closeTol)
            {
                const std::int64_t off = st.lastOffSeen >= 0 ? st.lastOffSeen : st.lastOn + st.lastInterval;
                emitOff(st.tremVoice, key, at(off + overlap, st.jitter));
                if (st.pickVoice != st.tremVoice)
                    emitOff(st.pickVoice, key, at(off + overlap, st.jitter)); // release the lingering pick
                st.phase = Phase::Idle;
                progress = true;
            }
        }
    }
}

void StreamingScheduler::tremoloActivity(bool& keyswitchTremolo, bool& detectorTremolo,
                                         bool& keyswitchTremoloLegato) const
{
    keyswitchTremolo = false;
    detectorTremolo = false;
    keyswitchTremoloLegato = false;
    for (const auto& kv : state_)
    {
        const KeyState& st = kv.second;
        if (st.phase == Phase::Idle)
            continue;
        if (st.articulation == Articulation::Tremolo)
        {
            keyswitchTremolo = true;                   // an explicit tremolo note is sounding
            if (st.legato)
                keyswitchTremoloLegato = true;         // and it is a slur continuation (Trem, no attack)
        }
        else if (st.phase == Phase::Tremolo && st.articulation == Articulation::Auto)
            detectorTremolo = true;                    // the detector is sustaining a tremolo
    }
}

void StreamingScheduler::forceCloseAll(std::int64_t atSample, std::vector<VoiceCommand>& out)
{
    const std::int64_t L = params_.lookaheadSamples;
    std::int64_t when = atSample + L;
    if (when < 0) when = 0;
    
    for (auto& kv : state_)
    {
        KeyState& st = kv.second;
        if (st.phase == Phase::Idle)
            continue;
            
        if (st.pickVoice > 0)
            out.push_back({when, VoiceCommandType::NoteOff, st.pickVoice, kv.first, 0, 0, 0, 0.0f, 0.0f});
        
        if (st.tremVoice > 0 && st.tremVoice != st.pickVoice)
            out.push_back({when, VoiceCommandType::NoteOff, st.tremVoice, kv.first, 0, 0, 0, 0.0f, 0.0f});
        
        st.phase = Phase::Idle;
        st.pickVoice = 0;
        st.tremVoice = 0;
    }
    
    perKey_.clear();
}

} // namespace plectro
