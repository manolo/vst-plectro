#include "Editor.h"
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
} // namespace
// instrumentNameForBank + populateInstrumentBox are shared, see EditorSupport.

Editor::Editor(PlectroProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p)
{
    setLookAndFeel(&lnf_);

    titleLabel_.setText(juce::String::fromUTF8(kEdition.productName).toUpperCase(), juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    titleLabel_.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(titleLabel_);

    channelInfoLabel_.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    channelInfoLabel_.setColour(juce::Label::textColourId, dim);
    channelInfoLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(channelInfoLabel_);

    footerLabel_.setText(editorFooterText(), juce::dontSendNotification);
    footerLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    footerLabel_.setColour(juce::Label::textColourId, dim);
    footerLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(footerLabel_);

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
    ksLedLabel_.setText("keyswitch", juce::dontSendNotification);
    winLedLabel_.setText("window", juce::dontSendNotification);
    for (auto* l : { &ksLedLabel_, &winLedLabel_ }) {
        l->setColour(juce::Label::textColourId, dim);
        l->setFont(juce::Font(juce::FontOptions(10.0f)));
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

    makeRotary(windowSlider_, windowLabel_, "Tremolo Window");
    makeRotary(gainSlider_, gainLabel_, "Gain");
    windowSlider_.setLookAndFeel(&lnfVintage_);
    gainSlider_.setLookAndFeel(&lnfVintage_);
    windowAtt_ = std::make_unique<APVTS::SliderAttachment>(processor_.state(), pid::detectWindowMs, windowSlider_);
    gainAtt_ = std::make_unique<APVTS::SliderAttachment>(processor_.state(), pid::masterGain, gainSlider_);

    windowSlider_.setTooltip("Largest gap between strokes still treated as one tremolo. Adapts to a ritardando.");
    windowLabel_.setTooltip("Detector window: the largest gap between repeated notes still merged into one tremolo.");
    gainSlider_.setTooltip("Output level.");

    for (auto* s : { &windowSlider_, &gainSlider_ })
        for (auto* ch : s->getChildren())
            if (auto* box = dynamic_cast<juce::Label*>(ch))
                box->setFont(juce::Font(juce::FontOptions(11.0f)));

    // Display-only components must not intercept the mouse, so a control's tooltip is never swallowed
    // by a purely visual component overlapping it.
    juce::Component* const displays[] = { &ksLed_, &winLed_, &ksLedLabel_, &winLedLabel_,
                                          &gainMeter_, &channelInfoLabel_, &titleLabel_, &footerLabel_ };
    for (juce::Component* c : displays)
        c->setInterceptsMouseClicks(false, false);

    rebuildInstrumentList();
    updateEnablement();
    startTimerHz(30); // poll tremolo activity for the indicator LEDs

    setResizable(false, false);
    setSize(460, 278);
}

Editor::~Editor()
{
    windowSlider_.setLookAndFeel(nullptr);
    gainSlider_.setLookAndFeel(nullptr);
    resetButton_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

juce::Slider& Editor::makeRotary(juce::Slider& s, juce::Label& l, const juce::String& name)
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

void Editor::rebuildInstrumentList()
{
    populateInstrumentBox(instrumentBox_, processor_.listPresets());

    const int curBank = static_cast<int>(processor_.state().getRawParameterValue(pid::instrumentBank)->load());
    if (instrumentBox_.getNumItems() > 0)
    {
        if (instrumentBox_.indexOfItemId(curBank + 1) >= 0)
            instrumentBox_.setSelectedId(curBank + 1, juce::dontSendNotification);
        else
            instrumentBox_.setSelectedItemIndex(0, juce::sendNotification);
    }
}

void Editor::instrumentChanged()
{
    const int id = instrumentBox_.getSelectedId();
    if (id <= 0)
        return;
    const int bank = id - 1;
    if (auto* param = processor_.state().getParameter(pid::instrumentBank))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(bank)));
}

void Editor::updateEnablement()
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

    repaint(); // section boxes are painted darker when disabled
}

void Editor::resetToDefaults()
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

void Editor::resized()
{
    auto area = getLocalBounds();

    // Copyright and version, pinned near the very bottom with a small padding (less than the side
    // margin) so it sits closer to the edge.
    footerLabel_.setBounds(area.removeFromBottom(18).reduced(14, 3));
    area = area.reduced(14);

    // Header at the very top: title, instrument selector, reset far right.
    auto header = area.removeFromTop(34);
    resetButton_.setBounds(header.removeFromRight(36).withSizeKeepingCentre(36, 32));
    header.removeFromRight(8);
    titleLabel_.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(6);
    instrumentBox_.setBounds(header.withSizeKeepingCentre(header.getWidth(), 26));

    // Channel/track name the host passed, just under the title. The row is always reserved (even
    // before the host sends the name) so the sections below do not jump up on first open and then
    // drop when the name arrives.
    area.removeFromTop(2);
    channelInfoLabel_.setBounds(area.removeFromTop(14));
    area.removeFromTop(2);

    auto place = [](juce::Rectangle<int> cell, juce::Slider& s, juce::Label& l) {
        cell.reduce(4, 4);
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
    };

    area.removeFromTop(12);

    const int gap = 10;
    const int cellH = 128;
    auto botTitle = area.removeFromTop(20);
    auto bot = area.removeFromTop(cellH + 16);
    // Tremolo and Output share the width evenly (output has only gain here).
    const int half = (bot.getWidth() - gap) / 2;
    tremBox_ = bot.removeFromLeft(half);
    bot.removeFromLeft(gap);
    outBox_ = bot;

    const int trX = tremBox_.getRight() - 44;
    captureTrillsButton_.setBounds(trX, botTitle.getY(), 40, 20);
    tremEnableButton_.setBounds(tremBox_.getX() + 4, botTitle.getY(),
                                juce::jmax(90, trX - 6 - (tremBox_.getX() + 4)), 20);
    outputEnableButton_.setBounds(outBox_.getX() + 4, botTitle.getY(), 100, 20);

    {
        auto ti = tremBox_.reduced(8);
        auto ledRow = ti.removeFromBottom(14);
        auto knobCell = ti.withSizeKeepingCentre(juce::jmin(ti.getWidth(), 108), juce::jmin(ti.getHeight(), 108));
        place(knobCell, windowSlider_, windowLabel_);

        auto winCell = ledRow.removeFromLeft(ledRow.getWidth() / 2);
        auto ksCell = ledRow;
        winLed_.setBounds(winCell.getX(), winCell.getCentreY() - 6, 12, 12);
        winLedLabel_.setBounds(winCell.getX() + 15, winCell.getCentreY() - 8, winCell.getWidth() - 15, 16);
        ksLed_.setBounds(ksCell.getX(), ksCell.getCentreY() - 6, 12, 12);
        ksLedLabel_.setBounds(ksCell.getX() + 15, ksCell.getCentreY() - 8, ksCell.getWidth() - 15, 16);
    }
    {
        auto oi = outBox_.reduced(8);
        auto gainMeterRow = oi.removeFromBottom(14);
        auto gainCell = oi.withSizeKeepingCentre(juce::jmin(oi.getWidth(), 108), juce::jmin(oi.getHeight(), 108));
        place(gainCell, gainSlider_, gainLabel_);
        gainMeter_.setBounds(gainMeterRow.reduced(8, 2));
    }
}

void Editor::paint(juce::Graphics& g)
{
    // Diagonal background gradient: black over most of the dialog, ramping to a dark bluish violet
    // only near the bottom-right corner (the Alcala / tuna colours). The section boxes below are
    // slightly translucent so the violet shows through, not just the margins.
    auto full = getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff08070b), full.getTopLeft(),
                            juce::Colour(0xff37205c), full.getBottomRight(), false);
    bg.addColour(0.50, juce::Colour(0xff110b1f)); // stays near black until 50%, then turns violet
    g.setGradientFill(bg);
    g.fillRect(full);

    struct Sec { juce::Rectangle<int> box; bool enabled; };
    const Sec secs[] = {
        { tremBox_, tremEnableButton_.getToggleState() },
        { outBox_, outputEnableButton_.getToggleState() },
    };
    for (const auto& s : secs)
    {
        if (s.box.isEmpty())
            continue;
        g.setColour((s.enabled ? section : section.darker(0.4f)).withAlpha(0.80f)); // let the gradient show through
        g.fillRoundedRectangle(s.box.toFloat(), 6.0f);
        g.setColour(panel);
        g.drawRoundedRectangle(s.box.toFloat().reduced(0.5f), 6.0f, 1.2f);
    }
}

void Editor::timerCallback()
{
    // Snap to the target once very close so an idle meter/LED settles instead of easing forever and
    // repainting 30x a second, which starves the tooltip timer.
    auto approach = [](float cur, float target) {
        const float k = target > cur ? 0.6f : 0.12f;
        float next = cur + (target - cur) * k;
        if (juce::jmax(next - target, target - next) < 0.002f)
            next = target;
        return next;
    };
    ksLevel_ = approach(ksLevel_, processor_.isKeyswitchTremoloActive() ? 1.0f : 0.0f);
    winLevel_ = approach(winLevel_, processor_.isDetectorTremoloActive() ? 1.0f : 0.0f);
    ksLed_.setLevel(ksLevel_);
    winLed_.setLevel(winLevel_);


    const float peak = processor_.outputLevel();
    const float db = juce::Decibels::gainToDecibels(peak, -60.0f);
    const float target = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    const float k = target > gainMeterLevel_ ? 0.7f : 0.10f;
    gainMeterLevel_ += (target - gainMeterLevel_) * k;
    if (juce::jmax(gainMeterLevel_ - target, target - gainMeterLevel_) < 0.002f)
        gainMeterLevel_ = target; // settle so an idle meter stops repainting
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
        // No relayout needed: the channel row is always reserved (see resized()).
    }
}

juce::AudioProcessorEditor* makeEditor(PlectroProcessor& processor)
{
    return new Editor(processor);
}

} // namespace plectro
