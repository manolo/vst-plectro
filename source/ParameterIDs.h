// Parameter identifiers and the APVTS layout. Kept in one place so the processor, editor
// and any host automation agree on names and ranges.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Types.h"
#include "core/KeyswitchLayout.h"

// Per-variant identity, injected by CMake (one build per instrument family). Fallbacks keep
// a single-plugin build working.
#ifndef PLECTRO_PRODUCT_NAME
#define PLECTRO_PRODUCT_NAME "Pulso y Pua"
#endif
#ifndef PLECTRO_DEFAULT_BANK
#define PLECTRO_DEFAULT_BANK 0
#endif

namespace plectro::pid {

// Sound
inline constexpr auto masterGain = "master_gain";
inline constexpr auto instrumentBank = "instrument_bank"; // SF2 bank = instrument model

// Master humanize enable
inline constexpr auto humanize = "humanize";

// Section enables (like humanize): tremolo processing and the output stage
inline constexpr auto tremoloOn = "tremolo_on";
inline constexpr auto outputOn = "output_on";

// Tremolo detection
inline constexpr auto detectWindowMs = "detect_window_ms";
inline constexpr auto minRepeats = "min_repeats";

// Capture notated trills and play them as a single sustained tremolo on the main note.
inline constexpr auto captureTrills = "capture_trills";

// Timing
inline constexpr auto jitterMs = "jitter_ms";
inline constexpr auto breathingDepthMs = "breathing_depth_ms";
inline constexpr auto breathingRateHz = "breathing_rate_hz";

// Dynamics
inline constexpr auto velocityCurve = "velocity_curve";
inline constexpr auto variationDepth = "variation_depth";
inline constexpr auto compression = "compression";

// Length / articulation
inline constexpr auto lengthVariation = "length_variation";
inline constexpr auto noteOverlapMs = "note_overlap_ms";

// Tuning imperfection
inline constexpr auto detuneCents = "detune_cents";

// Articulation addresses (SF2 bank + preset), routed independently
inline constexpr auto pickedBank = "picked_bank";
inline constexpr auto pickedPreset = "picked_preset";
inline constexpr auto tremoloBank = "tremolo_bank";
inline constexpr auto tremoloPreset = "tremolo_preset";
inline constexpr auto pizzicatoPreset = "pizzicato_preset";
inline constexpr auto harmonicPreset = "harmonic_preset";
inline constexpr auto mutePreset = "mute_preset";

// Keyswitch zone: note-ons in [kKeyswitchBase, kKeyswitchZoneTop] do not sound; they select
// the articulation for subsequent notes on the same MIDI channel (held until the next
// keyswitch). The bottom octave (MIDI 0..11, C-1..B-1) is reserved: no plucked-string
// instrument plays that low, and it matches MuseScore's own reserved keyswitch zone (idx<12).
inline constexpr int kKeyswitchBase = 0;      // C-1 = Picked (normal)
inline constexpr int kKeyswitchZoneTop = 12;  // C0: Legato modifier is the top of the reserved zone

// Decode a keyswitch note to its articulation, from the shared keyswitch layout (single source of
// truth with the IKeyswitchController advertiser). An unmapped keyswitch resets to normal.
inline plectro::Articulation articulationForKeyswitch(int note)
{
    return plectro::articulationForKeyswitchNote(note - kKeyswitchBase);
}

// Reproducibility / ensemble
inline constexpr auto globalSeed = "global_seed";
inline constexpr auto instanceSeed = "instance_seed";

// Look-ahead
inline constexpr auto lookaheadMs = "lookahead_ms";

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // The value text shown by host/editor comes from the PARAMETER (SliderAttachment uses it,
    // not the slider's own suffix), so format decimals + unit here.
    auto fmt = [](int dp, String unit) {
        return AudioParameterFloatAttributes().withStringFromValueFunction(
            [dp, unit](float v, int) { return String(v, dp) + unit; });
    };

    // Gain is expressed in dB (0 dB = unity), so typing "0" means unity, not silence. The
    // knob is skewed so 0 dB sits near the middle of the travel, not near the top.
    auto gainRange = NormalisableRange<float>(-36.0f, 12.0f);
    gainRange.setSkewForCentre(0.0f);
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{masterGain, 1}, "Master Gain", gainRange, 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(v, 1) + " dB"; })));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{detectWindowMs, 1}, "Tremolo Window", NormalisableRange<float>(40.0f, 400.0f, 1.0f), 150.0f, fmt(0, " ms")));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{minRepeats, 1}, "Tremolo Min Repeats", 2, 6, 2));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{captureTrills, 1}, "Capture Trills", true));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{jitterMs, 1}, "Timing Jitter", NormalisableRange<float>(0.0f, 40.0f), 8.5f, fmt(1, " ms")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{breathingDepthMs, 1}, "Breathing Depth", NormalisableRange<float>(0.0f, 60.0f), 0.0f, fmt(1, " ms")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{breathingRateHz, 1}, "Breathing Rate", NormalisableRange<float>(0.05f, 2.0f), 0.3f, fmt(2, " Hz")));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{velocityCurve, 1}, "Velocity Curve", NormalisableRange<float>(0.3f, 3.0f), 1.0f, fmt(2, "")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{variationDepth, 1}, "Dynamics Variation", NormalisableRange<float>(0.0f, 0.4f), 0.05f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; })));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{compression, 1}, "Dynamics Compression", NormalisableRange<float>(0.0f, 1.0f), 0.3f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; })));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{lengthVariation, 1}, "Length Variation", NormalisableRange<float>(0.0f, 0.4f), 0.0f, fmt(2, "")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{noteOverlapMs, 1}, "Note Overlap", NormalisableRange<float>(0.0f, 150.0f), 60.0f, fmt(0, " ms")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{detuneCents, 1}, "Detune", NormalisableRange<float>(0.0f, 15.0f), 4.0f, fmt(1, " cents")));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{instrumentBank, 1}, "Instrument", 0, 128, PLECTRO_DEFAULT_BANK));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{humanize, 1}, "Humanize", true));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{tremoloOn, 1}, "Tremolo", true));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{outputOn, 1}, "Output", true));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{pickedPreset, 1}, "Picked Preset", 0, 127, 0));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{tremoloPreset, 1}, "Tremolo Preset", 0, 127, 1));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{pizzicatoPreset, 1}, "Pizzicato Preset", 0, 127, 2));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{harmonicPreset, 1}, "Harmonic Preset", 0, 127, 3));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{mutePreset, 1}, "Mute Preset", 0, 127, 4));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{globalSeed, 1}, "Global Seed", 0, 1000000, 1));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{instanceSeed, 1}, "Instance Seed", 0, 1000000, 0));

    // Look-ahead is a fixed output shift reported as plugin latency. It is NOT needed for tremolo or
    // trill detection (that uses the detection window), it only gives headroom so the Pro timing
    // humanization can nudge a note earlier than written. A host that compensates plugin latency
    // cancels the shift; MuseScore does not support VST3 latency yet
    // (https://github.com/musescore/MuseScore/issues/34388), so any non-zero value there just delays
    // the audio with no benefit. Default 0. Revisit (a small non-zero default for symmetric timing
    // jitter) once that MuseScore issue is fixed. See specs/lookahead-latency.md.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{lookaheadMs, 1}, "Look-ahead", NormalisableRange<float>(0.0f, 60.0f), 0.0f));

    return layout;
}

} // namespace plectro::pid
