#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstring>

#include "core/KeyswitchLayout.h"

namespace plectro {

// Set true once a VST3 host queries our IKeyswitchController. A host that asks for the keyswitch
// map drives articulations by keyswitch and notates tremolo explicitly, so the plugin keeps its
// rhythmic tremolo detector off: that detector otherwise misfires on ornament expansions (trills,
// mordents, turns) that arrive as rapid repeated notes. Process-wide, and available in every
// plugin format (only ever set from the VST3 build below).
inline std::atomic<bool>& keyswitchControllerQueriedFlag()
{
    static std::atomic<bool> queried{ false };
    return queried;
}
inline bool hostQueriedKeyswitches()
{
    return keyswitchControllerQueriedFlag().load(std::memory_order_relaxed);
}

} // namespace plectro

// Only include VST3 SDK headers when building the VST3 plugin format
#if JucePlugin_Build_VST3

#include "pluginterfaces/vst/ivstnoteexpression.h"

namespace plectro {

// VST3 extension that exposes keyswitch information to hosts like Cubase and Dorico.
// This allows DAWs to automatically discover articulations and create expression maps.
class KeyswitchControllerExtension : public Steinberg::Vst::IKeyswitchController
{
public:
    KeyswitchControllerExtension() = default;

    // IKeyswitchController interface
    Steinberg::int32 PLUGIN_API getKeyswitchCount(Steinberg::int32 busIndex,
                                                   Steinberg::int16 channel) override
    {
        keyswitchControllerQueriedFlag().store(true, std::memory_order_relaxed);
        // Advertise every keyswitch in the shared layout on the first event bus, channel 0.
        if (busIndex == 0 && channel == 0)
            return static_cast<Steinberg::int32>(kKeyswitchLayout.size());
        return 0;
    }

    Steinberg::tresult PLUGIN_API getKeyswitchInfo(Steinberg::int32 busIndex,
                                                    Steinberg::int16 channel,
                                                    Steinberg::int32 keySwitchIndex,
                                                    Steinberg::Vst::KeyswitchInfo& info) override
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        if (busIndex != 0 || channel != 0
            || keySwitchIndex < 0 || keySwitchIndex >= static_cast<int32>(kKeyswitchLayout.size()))
            return kResultFalse;

        // Titles are the exact MuseScore articulation names, so the host matches them without any
        // heuristic. Every keyswitch is a single note, "press before noteOn" type.
        const KeyswitchDef& ks = kKeyswitchLayout[static_cast<std::size_t>(keySwitchIndex)];

        info.typeId = kNoteOnKeyswitchTypeID;
        info.keyswitchMin = ks.note;
        info.keyswitchMax = ks.note;
        info.keyRemapped = -1;  // no remapping
        info.unitId = -1;       // no unit
        info.flags = 0;

        // Convert to UTF-16 (VST3 requirement)
        juce::String(ks.name).copyToUTF16(reinterpret_cast<juce::CharPointer_UTF16::CharType*>(info.title), 128);
        juce::String(ks.name).copyToUTF16(reinterpret_cast<juce::CharPointer_UTF16::CharType*>(info.shortTitle), 128);

        return kResultTrue;
    }

    // FUnknown interface (required by all VST3 interfaces)
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override
    {
        using namespace Steinberg;
        
        // Compare against self-contained iid values (INLINE_UID is header-only) rather than the
        // SDK's linked FUnknown::iid / IKeyswitchController::iid statics. Those static members are
        // only linked into the VST3 target, but this shared code is also linked into the AU and
        // Standalone targets, which do not link the VST3 SDK; referencing them broke the AU link.
        static const TUID kFUnknownIid = INLINE_UID(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
        static const TUID kKeyswitchIid = INLINE_UID(0x1F2F76D3, 0xBFFB4B96, 0xB99527A5, 0x5EBCCEF4);

        if (std::memcmp(_iid, kFUnknownIid, sizeof(TUID)) == 0) {
            *obj = static_cast<FUnknown*>(this);
            addRef();
            return kResultTrue;
        }
        if (std::memcmp(_iid, kKeyswitchIid, sizeof(TUID)) == 0) {
            *obj = static_cast<Vst::IKeyswitchController*>(this);
            addRef();
            return kResultTrue;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};

} // namespace plectro

#endif // JucePlugin_Build_VST3
