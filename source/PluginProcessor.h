#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "ParameterIDs.h"
#include "engine/SoundFontEngine.h"
#include "core/Types.h"
#include "core/StreamingScheduler.h"
#include "core/LayerSet.h"
#include "core/DetectionGate.h"
#include "KeyswitchSupport.h"

namespace plectro {

class PlectroProcessor : public juce::AudioProcessor
{
public:
    PlectroProcessor();
    ~PlectroProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String::fromUTF8(PLECTRO_PRODUCT_NAME); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;

#if JucePlugin_Build_VST3
    // Expose keyswitch information to VST3 hosts (Cubase, Dorico, MuseScore) for automatic
    // expression map creation. Returns an interface that implements Steinberg::Vst::IKeyswitchController.
    juce::VST3ClientExtensions* getVST3ClientExtensions() override
    {
        static KeyswitchControllerExtension keyswitchExtension;
        static struct VST3Extensions : public juce::VST3ClientExtensions
        {
            int32_t queryIEditController(const Steinberg::TUID iid, void** obj) override
            {
                return keyswitchExtension.queryInterface(iid, obj);
            }
        } vst3Extensions;
        return &vst3Extensions;
    }
#endif


    // The host (via VST3 channel context / AU) tells us the track name; we auto-select the
    // instrument bank from it (bandurria / laud / mandolina) and expose it for the editor.
    void updateTrackProperties(const TrackProperties& properties) override;
    juce::String hostTrackName() const { return hostTrackName_; } // message-thread only

    juce::AudioProcessorValueTreeState& state() { return apvts_; }

    // SF2 path is managed outside the APVTS (strings do not fit the float parameter model).
    void setSoundFontPath(const juce::String& path);
    void useBundledSoundFont(); // switch back to the edition's bundled font
    juce::String getSoundFontPath() const { return sf2Path_; }

    // Presets of the loaded SF2, for the instrument picker in the editor.
    std::vector<SoundFontEngine::PresetInfo> listPresets() { return engine_.listPresets(); }

    // Live tremolo activity for the editor's indicator LEDs (audio thread -> UI thread).
    bool isKeyswitchTremoloActive() const { return keyswitchTremoloActive_.load(std::memory_order_relaxed); }
    bool isDetectorTremoloActive() const { return detectorTremoloActive_.load(std::memory_order_relaxed); }
    bool isKeyswitchTremoloLegato() const { return keyswitchTremoloLegato_.load(std::memory_order_relaxed); }

    // Output peak level (0..1) for the VU meter, and the last played note-on velocity (1..127)
    // for the compression-direction indicator.
    float outputLevel() const { return outputLevel_.load(std::memory_order_relaxed); }
    int lastInputVelocity() const { return lastInputVelocity_.load(std::memory_order_relaxed); }


    // True once a host keyswitch has been seen this playback session (reset on transport stop).
    // While true the rhythmic detector is off, so the instrument trusts the host's keyswitches.

private:
    PlaybackParams readParams() const;
    juce::String bundledSoundFontPath() const; // <bundle>/Contents/Resources/<edition SF2>
    void ingestMidi(const juce::MidiBuffer& midi);
    void renderScheduled(juce::AudioBuffer<float>& buffer);
    void reloadSoundFont(); // load sf2Path_, falling back to the bundled font if it does not exist
    void rebuildPresetMap(); // derive articulation -> preset from the loaded SF2's preset names
    void rebuildFamilyBanks(); // which SF2 banks are present per family, for the "All" ensembles

    // Ensemble layers. A normal instrument is a single layer; an "All <family>" selection is a
    // central dehumanized anchor plus humanized copies (see core/LayerSet.h). Reconfigure
    // rebuilds the active layers when the instrument selection changes, closing any open voices.
    void reconfigureLayers(int selection, std::int64_t atSample, std::vector<VoiceCommand>& offs);
    void pushToLayers(const NoteEvent& e);
    void closeLayersOnBoundary(std::int64_t atSample); // span boundary: close every layer's voices

    juce::AudioProcessorValueTreeState apvts_;
    SoundFontEngine engine_;

    // A humanizer per rendered layer, plus its routing (bank, seed offset, gain, neutralize).
    struct Layer
    {
        StreamingScheduler stream;
        int bank = 0;
        bool neutralize = false; // central anchor: force per note humanization off
        int seedOffset = 0;      // decorrelates this layer from the others
        float gainMul = 1.0f;    // per layer relative gain (central anchor trimmed ~-3 dB)
        float pan = 0.0f;        // stereo placement -1 left .. 0 centre .. +1 right
    };
    static constexpr int kMaxLayers = 12;          // largest family (10 banks) + 1 central + slack
    static constexpr int kVoiceStride = 1 << 22;   // per layer voice id offset, so voices stay distinct
    std::vector<Layer> layers_;                    // sized kMaxLayers in prepareToPlay
    int activeLayers_ = 1;
    int lastSelection_ = INT_MIN;                  // instrument selection last configured (forces first reconfig)

    // Present SF2 banks per family (index 0 bandurria, 1 laud, 2 mandolina), built at load on the
    // message thread and read on the audio thread (same threading stance as presetMap_).
    std::array<std::array<int, 10>, 3> familyBanks_{};
    std::array<int, 3> familyBankCount_{};

    juce::String sf2Path_;

    // Once a project restores its state, the instrument selection is owned by the project and the
    // host's track-name auto-mapping (updateTrackProperties) must not overwrite it on reload. Stays
    // false for a fresh instance, so dropping the plugin on a named track still auto-selects.
    bool instrumentPinned_ = false;

    double sampleRate_ = 44100.0;
    std::int64_t lookaheadSamples_ = 0;
    std::int64_t hostSample_ = 0;              // absolute input position at the start of the block

    std::vector<VoiceCommand> scheduled_;      // output commands, sorted by targetSample
    std::size_t schedPos_ = 0;                 // next command to execute

    std::atomic<bool> keyswitchTremoloActive_{ false }; // a keyswitch (explicit) tremolo is sounding
    std::atomic<bool> detectorTremoloActive_{ false };  // the rhythmic detector is sustaining a tremolo
    std::atomic<bool> keyswitchTremoloLegato_{ false }; // the sounding keyswitch tremolo is a slur continuation
    std::atomic<float> outputLevel_{ 0.0f };            // output peak (0..1) for the VU meter
    std::atomic<int> lastInputVelocity_{ 64 };          // last played note-on velocity

    std::atomic<bool> keyswitchSeen_{ false };          // a keyswitch was seen this session
    std::atomic<int> musicalNotesSinceStart_{ 0 };      // musical notes this session (for shouldAutoDetect)
    bool wasPlaying_ = false;                           // transport state, to detect Stop
    juce::String hostTrackName_;                        // last track name from the host (message thread)

    // Current articulation per MIDI channel (1..16; index 0 unused), set by keyswitch notes
    // and held until the next keyswitch. Stamped onto every playable note as it is ingested.
    std::array<Articulation, 17> articulationLatch_{}; // value-initialised to Articulation::Auto
    std::array<bool, 17> legatoActive_{};  // per-channel legato span state (keyswitch on..off)
    std::array<bool, 17> legatoNoteSeen_{}; // a note has sounded in the active legato span (per channel)

    // Trill: rendered as a single sustained tremolo on the main note. A trill keyswitch marks the
    // channel; the first trill note is the main pitch (kept, played as tremolo), and the trill's
    // alternating upper note is dropped, so one tremolo sounds instead of two beating together.
    std::array<bool, 17> trillActive_{};
    std::array<int, 17> trillMainKey_{};

    // Articulation -> SF2 preset, derived from the loaded SoundFont's preset names (see
    // rebuildPresetMap) rather than hard-coded indices, so routing follows the SF2 layout.
    // Written on the message thread at load, read on the audio thread in readParams.
    std::array<std::atomic<int>, kNumArticulations> presetMap_{};
    std::atomic<int> tremoloPickedPreset_{ 2 }; // P+T preset (standalone tremolo); mirrors presetMap_

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlectroProcessor)
};

} // namespace plectro
