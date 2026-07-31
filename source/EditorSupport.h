// The plugin's editor factory. The processor calls makeEditor() without knowing which edition it
// is: the free build links a compact editor and the Pro build links its own full editor, so the
// humanization editor lives only in the Pro source. Selected at link time, no preprocessor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace plectro {

class PlectroProcessor;

juce::AudioProcessorEditor* makeEditor(PlectroProcessor& processor);

} // namespace plectro
