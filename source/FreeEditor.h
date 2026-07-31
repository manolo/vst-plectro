#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "EditorLookAndFeel.h"

namespace plectro {

// The free edition editor: instrument picker, tremolo section and output gain. No humanization
// controls and no custom SoundFont loading, so it is a compact window and ships in the public repo.
class FreeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FreeEditor(PlectroProcessor& p);
    ~FreeEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void timerCallback() override;
    void rebuildInstrumentList();
    void instrumentChanged();
    void resetToDefaults();
    void updateEnablement();
    juce::Slider& makeRotary(juce::Slider& s, juce::Label& l, const juce::String& name);

    PlectroProcessor& processor_;
    HumanLookAndFeel lnf_;
    GlyphLookAndFeel glyphLnf_;
    KnobVintage lnfVintage_;

    juce::Label titleLabel_, channelInfoLabel_;
    juce::Label footerLabel_;            // bottom line: copyright and version
    int lastShownBank_ = -1;
    juce::String lastChannelName_;

    juce::ComboBox instrumentBox_;

    juce::ToggleButton tremEnableButton_ { "Tremolo" };
    juce::ToggleButton captureTrillsButton_ { "tr" };
    TremoloLed ksLed_, winLed_;
    juce::Label ksLedLabel_, winLedLabel_;
    float ksLevel_ = 0.0f, winLevel_ = 0.0f;
    juce::Slider windowSlider_;
    juce::Label windowLabel_;

    juce::ToggleButton outputEnableButton_ { "Output" };
    juce::Slider gainSlider_;
    juce::Label gainLabel_;
    LevelMeter gainMeter_;
    float gainMeterLevel_ = 0.0f;
    juce::TextButton resetButton_ { "Reset" };

    std::unique_ptr<APVTS::ButtonAttachment> tremEnableAtt_, outputEnableAtt_, captureTrillsAtt_;
    std::unique_ptr<APVTS::SliderAttachment> windowAtt_, gainAtt_;

    juce::TooltipWindow tooltip_ { this, 500 };
    juce::Rectangle<int> tremBox_, outBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeEditor)
};

} // namespace plectro
