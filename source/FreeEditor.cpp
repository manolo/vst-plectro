#include "FreeEditor.h"
#include "EditorSupport.h"
#include "ParameterIDs.h"
#include "Edition.h"

namespace plectro {

namespace {
using palette::accent;
using palette::background;
using palette::dim;
using palette::panel;
using palette::section;
using palette::text;

// The instrument shown as the editor title, from the SF2 bank family. All families and out of
// range banks fall back to the product name.
juce::String instrumentNameForBank(int bank)
{
    if (bank == kAllBandurria) return "All Bands";
    if (bank == kAllLaud)      return "All Laudes";
    if (bank == kAllMandolina) return "All Mandolins";
    if (bank >= 0 && bank <= 9)   return "Bandurria";
    if (bank >= 10 && bank <= 19) return juce::String::fromUTF8("La\xc3\xba" "d");
    if (bank >= 20 && bank <= 29) return "Mandolina";
    return juce::String::fromUTF8("Pulso y P\xc3\xba" "a");
}
} // namespace

FreeEditor::FreeEditor(PlectroProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p)
{
    setLookAndFeel(&lnf_);

    titleLabel_.setText(juce::String::fromUTF8(kEdition.productName).toUpperCase(), juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    titleLabel_.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(titleLabel_);

    channelInfoLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    channelInfoLabel_.setColour(juce::Label::textColourId, dim);
    channelInfoLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(channelInfoLabel_);

    instrumentBox_.onChange = [this] { instrumentChanged(); };
    addAndMakeVisible(instrumentBox_);

    addAndMakeVisible(tremEnableButton_);
    tremEnableAtt_ = std::make_unique<APVTS::ButtonAttachment>(processor_.state(), pid::tremoloOn, tremEnableButton_);
    tremEnableButton_.onClick = [this] { updateEnablement(); };
    tremEnableButton_.setTooltip("Enable tremolo (keyswitch and detector). Off: all notes play picked.");

    addAndMakeVisible(captureTrillsButton_);
    captureTrillsAtt_ = std::make_unique<APVTS::ButtonAttachment>(processor_.state(), pid::captureTrills, captureTrillsButton_);
    captureTrillsButton_.setTooltip("Play notated trills as a single sustained tremolo on the main note.");

    ksLed_.colour = juce::Colour(0xff6fcf6f); // green: tremolo coming from a host keyswitch
    winLed_.colour = accent;                  // warm: tremolo coming from the detector
    addAndMakeVisible(ksLed_);
    addAndMakeVisible(winLed_);
    ksLedLabel_.setText("KS", juce::dontSendNotification);
    winLedLabel_.setText("WIN", juce::dontSendNotification);
    for (auto* l : { &ksLedLabel_, &winLedLabel_ }) {
        l->setColour(juce::Label::textColourId, dim);
        l->setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(*l);
    }

    addAndMakeVisible(gainMeter_);

    addAndMakeVisible(outputEnableButton_);
    outputEnableAtt_ = std::make_unique<APVTS::ButtonAttachment>(processor_.state(), pid::outputOn, outputEnableButton_);
    outputEnableButton_.onClick = [this] { updateEnablement(); };
    outputEnableButton_.setTooltip("Enable the output stage (gain). Off: unity gain.");

    resetButton_.setButtonText(juce::String::fromUTF8("\xE2\x86\xBA")); // circular arrow
    resetButton_.setTooltip("Reset all except instrument");
    resetButton_.setLookAndFeel(&glyphLnf_);
    resetButton_.onClick = [this] { resetToDefaults(); };
    addAndMakeVisible(resetButton_);

    makeRotary(windowSlider_, windowLabel_, "Tremolo Win");
    makeRotary(gainSlider_, gainLabel_, "Gain");
    windowSlider_.setLookAndFeel(&lnfVintage_);
    gainSlider_.setLookAndFeel(&lnfVintage_);
    windowAtt_ = std::make_unique<APVTS::SliderAttachment>(processor_.state(), pid::detectWindowMs, windowSlider_);
    gainAtt_ = std::make_unique<APVTS::SliderAttachment>(processor_.state(), pid::masterGain, gainSlider_);

    windowSlider_.setTooltip("Largest gap between strokes still treated as one tremolo. Adapts to a ritardando.");
    windowLabel_.setTooltip("Detector window: the largest gap between repeated notes still merged into one tremolo. Disabled while a host keyswitch tremolo is playing.");
    gainSlider_.setTooltip("Output level.");

    for (auto* s : { &windowSlider_, &gainSlider_ })
        for (auto* ch : s->getChildren())
            if (auto* box = dynamic_cast<juce::Label*>(ch))
                box->setFont(juce::Font(juce::FontOptions(11.0f)));

    rebuildInstrumentList();
    updateEnablement();
    startTimerHz(30); // poll tremolo activity for the indicator LEDs

    setResizable(true, true);
    setSize(460, 218);
}

FreeEditor::~FreeEditor()
{
    windowSlider_.setLookAndFeel(nullptr);
    gainSlider_.setLookAndFeel(nullptr);
    resetButton_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

juce::Slider& FreeEditor::makeRotary(juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 13);
    addAndMakeVisible(s);

    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, dim);
    l.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(l);
    return s;
}

void FreeEditor::rebuildInstrumentList()
{
    instrumentBox_.clear(juce::dontSendNotification);
    const auto presets = processor_.listPresets();
    bool hasBand = false, hasLaud = false, hasMand = false;
    for (const auto& pr : presets)
    {
        if (pr.preset != 0)
            continue;
        const char* family = (pr.bank <= 9) ? "Band: " : (pr.bank <= 19) ? "Laud: " : (pr.bank <= 29) ? "Mand: " : "";
        if (pr.bank <= 9)       hasBand = true;
        else if (pr.bank <= 19) hasLaud = true;
        else if (pr.bank <= 29) hasMand = true;
        juce::String name = pr.name;
        if (name.length() > 2 && name[1] == ' ')
            name = name.substring(2);
        instrumentBox_.addItem(juce::String(family) + name, pr.bank + 1); // itemId cannot be 0
    }

    if (hasBand || hasLaud || hasMand)
        instrumentBox_.addSeparator();
    if (hasBand) instrumentBox_.addItem("All Bands", kAllBandurria + 1);
    if (hasLaud) instrumentBox_.addItem("All Laudes", kAllLaud + 1);
    if (hasMand) instrumentBox_.addItem("All Mandolins", kAllMandolina + 1);

    const int curBank = static_cast<int>(processor_.state().getRawParameterValue(pid::instrumentBank)->load());
    if (instrumentBox_.getNumItems() > 0)
    {
        if (instrumentBox_.indexOfItemId(curBank + 1) >= 0)
            instrumentBox_.setSelectedId(curBank + 1, juce::dontSendNotification);
        else
            instrumentBox_.setSelectedItemIndex(0, juce::sendNotification);
    }
}

void FreeEditor::instrumentChanged()
{
    const int id = instrumentBox_.getSelectedId();
    if (id <= 0)
        return;
    const int bank = id - 1;
    if (auto* param = processor_.state().getParameter(pid::instrumentBank))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(bank)));
}

void FreeEditor::updateEnablement()
{
    const bool tremOn = tremEnableButton_.getToggleState();
    ksLed_.setEnabled(tremOn);
    winLed_.setEnabled(tremOn);
    ksLedLabel_.setEnabled(tremOn);
    winLedLabel_.setEnabled(tremOn);
    captureTrillsButton_.setEnabled(tremOn);
    windowSlider_.setEnabled(tremOn);
    windowLabel_.setEnabled(tremOn);

    const bool outOn = outputEnableButton_.getToggleState();
    gainSlider_.setEnabled(outOn);
    gainLabel_.setEnabled(outOn);
    gainMeter_.setEnabled(outOn);
}

void FreeEditor::resetToDefaults()
{
    for (auto* param : processor_.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (withId->paramID == pid::instrumentBank || withId->paramID == pid::instanceSeed)
                continue; // keep the chosen instrument and this instance's random seed
        param->setValueNotifyingHost(param->getDefaultValue());
    }
    processor_.useBundledSoundFont(); // return to the internal font
    rebuildInstrumentList();
    updateEnablement();
}

void FreeEditor::resized()
{
    auto area = getLocalBounds().reduced(14);

    channelInfoLabel_.setBounds(area.removeFromTop(14));
    area.removeFromTop(2);

    auto header = area.removeFromTop(34);
    resetButton_.setBounds(header.removeFromRight(36).withSizeKeepingCentre(36, 32));
    header.removeFromRight(8);
    titleLabel_.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(6);
    instrumentBox_.setBounds(header.withSizeKeepingCentre(header.getWidth(), 26));

    auto place = [](juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l) {
        cell.reduce(4, 4);
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
    };

    area.removeFromTop(12);

    const int gap = 10;
    const int cellH = 92;
    auto botTitle = area.removeFromTop(20);
    auto bot = area.removeFromTop(cellH + 16);
    // Tremolo and Output share the width evenly (output has only gain here).
    const int half = (bot.getWidth() - gap) / 2;
    tremBox_ = bot.removeFromLeft(half);
    bot.removeFromLeft(gap);
    outBox_ = bot;

    tremEnableButton_.setBounds(tremBox_.getX() + 4, botTitle.getY(), 130, 20);
    captureTrillsButton_.setBounds(tremBox_.getRight() - 44, botTitle.getY(), 40, 20);
    outputEnableButton_.setBounds(outBox_.getX() + 4, botTitle.getY(), 100, 20);

    {
        auto ti = tremBox_.reduced(8);
        auto ledRow = ti.removeFromBottom(14);
        auto knobCell = ti.withSizeKeepingCentre(juce::jmin(ti.getWidth(), 84), juce::jmin(ti.getHeight(), 84));
        place(knobCell, windowSlider_, windowLabel_);

        auto winCell = ledRow.removeFromLeft(ledRow.getWidth() / 2);
        auto ksCell = ledRow;
        winLed_.setBounds(winCell.getX(), winCell.getCentreY() - 6, 12, 12);
        winLedLabel_.setBounds(winCell.getX() + 15, winCell.getCentreY() - 8, 34, 16);
        ksLed_.setBounds(ksCell.getX(), ksCell.getCentreY() - 6, 12, 12);
        ksLedLabel_.setBounds(ksCell.getX() + 15, ksCell.getCentreY() - 8, 34, 16);
    }
    {
        auto oi = outBox_.reduced(8);
        auto gainMeterRow = oi.removeFromBottom(14);
        auto gainCell = oi.withSizeKeepingCentre(juce::jmin(oi.getWidth(), 96), oi.getHeight());
        place(gainCell, gainSlider_, gainLabel_);
        gainMeter_.setBounds(gainMeterRow.reduced(8, 2));
    }
}

void FreeEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    for (const auto& b : { tremBox_, outBox_ })
    {
        if (b.isEmpty())
            continue;
        g.setColour(section);
        g.fillRoundedRectangle(b.toFloat(), 6.0f);
        g.setColour(panel);
        g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 6.0f, 1.2f);
    }
}

void FreeEditor::timerCallback()
{
    auto approach = [](float cur, float target) {
        const float k = target > cur ? 0.6f : 0.12f;
        return cur + (target - cur) * k;
    };
    ksLevel_ = approach(ksLevel_, processor_.isKeyswitchTremoloActive() ? 1.0f : 0.0f);
    winLevel_ = approach(winLevel_, processor_.isDetectorTremoloActive() ? 1.0f : 0.0f);
    ksLed_.setLevel(ksLevel_);
    winLed_.setLevel(winLevel_);

    const bool tremOn = tremEnableButton_.getToggleState();
    const bool ksSession = processor_.isKeyswitchSessionActive();
    windowSlider_.setEnabled(tremOn && !ksSession);
    windowLabel_.setEnabled(tremOn && !ksSession);

    const float peak = processor_.outputLevel();
    const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
    const float target = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    const float k = target > gainMeterLevel_ ? 0.7f : 0.10f;
    gainMeterLevel_ += (target - gainMeterLevel_) * k;
    gainMeter_.setLevel(gainMeterLevel_);

    const int bank = static_cast<int>(processor_.state().getRawParameterValue(pid::instrumentBank)->load());
    if (bank != lastShownBank_) {
        lastShownBank_ = bank;
        titleLabel_.setText(instrumentNameForBank(bank).toUpperCase(), juce::dontSendNotification);
    }

    const juce::String ch = processor_.hostTrackName();
    if (ch != lastChannelName_) {
        lastChannelName_ = ch;
        channelInfoLabel_.setText(ch.isEmpty() ? juce::String() : "Channel: " + ch, juce::dontSendNotification);
    }
}

juce::AudioProcessorEditor* makeEditor(PlectroProcessor& processor)
{
    return new FreeEditor(processor);
}

} // namespace plectro
