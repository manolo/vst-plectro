// Thin adapter around a SoundFont synth. FluidSynth is the reference backend; the interface
// is deliberately small (load / noteOn / setGain / noteOff / render) so it can be swapped
// for sfizz or mocked in tests. Uses plain float buffers, no JUCE types.
#pragma once

#include <string>
#include <array>
#include <vector>
#include <cstdint>

// Match FluidSynth's own opaque typedefs (fluidsynth/types.h) so this header stays
// self-contained for translation units that do not include <fluidsynth.h>.
struct _fluid_hashtable_t;
struct _fluid_synth_t;
typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;

namespace plectro {

class SoundFontEngine
{
public:
    SoundFontEngine();
    ~SoundFontEngine();

    SoundFontEngine(const SoundFontEngine&) = delete;
    SoundFontEngine& operator=(const SoundFontEngine&) = delete;

    // Prepare the synth for a sample rate. Safe to call again on rate changes.
    void prepare(double sampleRate);

    // Load an SF2 from disk. Returns true on success. Off the audio thread.
    bool loadSoundFont(const std::string& path);

    bool isLoaded() const { return loaded_; }

    struct PresetInfo
    {
        int bank = 0;
        int preset = 0;
        std::string name;
    };

    // Enumerate the presets of the loaded SF2 (for the instrument picker).
    std::vector<PresetInfo> listPresets() const;

    // Voice control. voiceId correlates the note across its lifetime so each note can own an
    // independent synth channel and therefore an independent expression (dynamics) value.
    // The bank + preset select the articulation (picked vs tremolo) on that channel.
    // detuneCents applies a pitch offset (fret imperfection / ensemble detune) via pitch bend.
    // pan places the voice in the stereo field (-1 left .. 0 centre .. +1 right) via CC10.
    void noteOn(int voiceId, int key, int velocity, float gain, int bank, int preset, float detuneCents, float pan);
    void setGain(int voiceId, float gain);
    void setPitch(int voiceId, float detuneCents);
    void noteOff(int voiceId, int key);
    void allNotesOff();

    // Overall output gain applied after synthesis.
    void setMasterGain(float g) { masterGain_ = g; }

    // Render interleaved-free stereo (separate L/R buffers) of numSamples frames.
    void render(float* left, float* right, int numSamples);

private:
    int channelForVoice(int voiceId);
    void freeVoiceChannel(int voiceId);

    // These are only touched when the FluidSynth backend is compiled in; mark them so the
    // silent build stays warning-clean.
    [[maybe_unused]] fluid_settings_t* settings_ = nullptr;
    [[maybe_unused]] fluid_synth_t* synth_ = nullptr;
    [[maybe_unused]] int sfId_ = -1;
    bool loaded_ = false;
    double sampleRate_ = 44100.0;
    float masterGain_ = 1.0f;

    // Enough channels for the ensemble ("All <family>") mode: up to a dozen layers, each needing
    // its own channels for polyphony (picked + tremolo voices per key). Idle channels are free in
    // FluidSynth, so a generous pool only costs when voices actually sound.
    static constexpr int kNumChannels = 128;
    // Round-robin channel pool. -1 means free; otherwise holds the owning voiceId.
    std::array<int, kNumChannels> channelVoice_ {};
    int nextChannel_ = 0;
};

} // namespace plectro
