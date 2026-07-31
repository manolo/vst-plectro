// Core data model for the humanizer. This header is JUCE-free and engine-free so the
// logic can be unit tested on its own.
#pragma once

#include <cstdint>
#include <vector>

namespace plectro {

// Articulation for a note. Auto = no explicit keyswitch was received, so infer from rhythm
// (immediate picked attack with rhythmic tremolo promotion: the historical behaviour). The
// others are selected by a keyswitch and pin the note to a specific SF2 preset.
enum class Articulation { Auto = 0, Picked, Tremolo, Pizzicato, Harmonic, Mute };
inline constexpr int kNumArticulations = 6;

// A "span" articulation is rendered as a single sustained gesture built from many rapid
// sub-notes (tremolo), rather than one voice per note. When the host re-sends its keyswitch to
// mark a new span (or to leave the span), the plugin must restart the gesture so consecutive
// spans do not merge into one sustain. Point articulations (picked, pizzicato, harmonic, mute)
// are one voice per note and must never trigger such a restart.
inline bool isSpanArticulation(Articulation a) { return a == Articulation::Tremolo; }

// A raw MIDI note event as it arrives from the host, placed on an absolute sample
// timeline. Only note-on / note-off are modelled; controllers are handled elsewhere.
struct NoteEvent
{
    std::int64_t sample = 0; // absolute sample position on the host timeline
    int key = 60;            // MIDI note number 0..127
    int velocity = 0;        // 1..127 for note-on, 0 for note-off
    bool isNoteOn = false;
    int channel = 1;         // MIDI channel 1..16 (kept for per-channel articulation latch)
    Articulation articulation = Articulation::Auto; // stamped from the latch on note-on
};

// The result of tremolo detection: a musical note with a decided articulation. A tremolo
// note carries the sequence of dynamic points that were present in the original burst so a
// crescendo across the tremolo can be preserved.
struct ArticulatedNote
{
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;
    int key = 60;
    bool isTremolo = false;

    // (sample, velocity) points describing how the notated dynamic evolves over the note.
    // For a picked note this holds a single entry at startSample.
    std::vector<std::pair<std::int64_t, int>> dynamicPoints;

    int firstVelocity() const
    {
        return dynamicPoints.empty() ? 0 : dynamicPoints.front().second;
    }
};

// Commands the engine adapter executes. Times are absolute sample positions on the output
// timeline (input time shifted by the fixed look-ahead latency).
enum class VoiceCommandType
{
    NoteOn,
    NoteOff,
    SetGain,
    SetPitch
};

struct VoiceCommand
{
    std::int64_t targetSample = 0;
    VoiceCommandType type = VoiceCommandType::NoteOn;
    int voiceId = 0;    // correlates NoteOn / SetGain / NoteOff of the same note
    int key = 60;       // valid for NoteOn / NoteOff
    int velocity = 0;   // trigger velocity handed to the engine (NoteOn)
    int preset = 0;     // SF2 preset for the articulation (NoteOn)
    int bank = 0;       // SF2 bank for the articulation (NoteOn)
    float gain = 1.0f;  // linear dynamics 0..1 (NoteOn / SetGain)
    float detuneCents = 0.0f; // pitch offset in cents (NoteOn / SetPitch)
};

// All humanization parameters. Populated from the plugin's APVTS each block.
struct HumanizerParams
{
    double sampleRate = 44100.0;

    // Tremolo. The plugin is always in Auto: the host tremolo keyswitch is used when present, and
    // the rhythmic detector merges repeated notes otherwise. tremoloOn is the master enable;
    // enableDetection follows it (kept as a field for the batch Humanizer/TremoloDetector).
    bool tremoloOn = true;
    bool enableDetection = true;
    double detectWindowMs = 90.0; // max gap between repeated note-ons to count as tremolo
    int minRepeats = 2;           // note-ons needed within the window to trigger tremolo

    // Timing humanization.
    double jitterMs = 8.0;        // std-dev of per-note Gaussian onset jitter
    double breathingDepthMs = 0.0;// amplitude of the slow phrase drift
    double breathingRateHz = 0.3; // rate of the slow phrase drift

    // Dynamics humanization.
    double velocityCurve = 1.0;   // exponent mapping velocity -> gain
    double variationDepth = 0.05; // std-dev of per-note gain variation (0..1)
    double phraseShape = 0.0;     // reserved: phrase-level crescendo/dim amount
    double compression = 0.0;     // 0 = keep dynamics, 1 = flatten toward the middle

    // Note length / legato.
    double lengthVariation = 0.0; // fractional variation of note length
    double legatoOverlapMs = 0.0; // reserved

    // Tuning imperfection (de-humanization).
    double detuneCents = 0.0;     // per-note random spread + per-instance offset, in cents

    // Instrument = SF2 bank; articulation = preset within it. Indexed by Articulation; Auto
    // and Picked share the picked preset. Velocity/CC carry dynamics.
    int bank = 0;
    int presetByArticulation[kNumArticulations] = { 0, 0, 1, 2, 3, 4 };
    int engineVelocity = 127;     // fixed trigger velocity for full sample amplitude

    int presetFor(Articulation a) const { return presetByArticulation[static_cast<int>(a)]; }

    // Legacy velocity bands, used only by the batch Humanizer (velocity-split fonts).
    int pickedVelocity = 64;
    int tremoloVelocity = 127;

    // Reproducibility.
    std::uint64_t globalSeed = 1;
    std::uint64_t instanceSeed = 0;

    // Fixed look-ahead latency in samples. Every output command is shifted by this so a
    // "rushed" note never lands in the past.
    std::int64_t lookaheadSamples = 0;

    // Samples per beat from the host tempo (0 = unknown, a default is used). Bounds how long an
    // explicit tremolo waits for its next stroke before the stroke interval is known: a tremolo
    // always subdivides the beat, so the next stroke is guaranteed within one beat, at any tempo.
    std::int64_t beatSamples = 0;

    std::int64_t detectWindowSamples() const
    {
        return static_cast<std::int64_t>(detectWindowMs * 0.001 * sampleRate);
    }
};

// Zero the per note expressive variation (used by the Humanize toggle and by editions
// that ship without humanization). Tremolo detection and articulation routing are not
// touched.
inline void neutralizePerNoteVariation(HumanizerParams& p)
{
    p.jitterMs = 0.0;
    p.breathingDepthMs = 0.0;
    p.variationDepth = 0.0;
    p.lengthVariation = 0.0;
    p.legatoOverlapMs = 0.0;
    p.detuneCents = 0.0;
}

} // namespace plectro
