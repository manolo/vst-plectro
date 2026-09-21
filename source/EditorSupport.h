// The plugin's editor factory. The processor calls makeEditor() without knowing which edition it
// is: the free build links a compact editor and the Pro build links its own full editor, so the
// humanization editor lives only in the Pro source. Selected at link time, no preprocessor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/SoundFontEngine.h"

#include <vector>

namespace plectro {

class PlectroProcessor;

juce::AudioProcessorEditor* makeEditor(PlectroProcessor& processor);

// Shared instrument picker helpers (used by both editors).
// Populate the combo from the loaded SF2 presets: each family's instruments ("Band: <name>", ...)
// followed by that family's "Band: All" ensemble entry, grouped and separated per family.
void populateInstrumentBox(juce::ComboBox& box, const std::vector<SoundFontEngine::PresetInfo>& presets);
// The editor title for an instrument bank or an All-family sentinel: the family name (so "Band: All"
// reads as BANDURRIA, like a single bandurria).
juce::String instrumentNameForBank(int bank);

// Plugin version shown in the editor footer. Fixed for now; a later build step may compute it.
inline constexpr const char* kPluginVersion = "0.2.1";
// The one-line footer at the bottom of both editors: copyright and version.
juce::String editorFooterText();

} // namespace plectro
