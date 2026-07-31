// Shared editor look and feel and small indicator components, used by both the free and the Pro
// editors. Pure presentation (no humanization, no Pro logic), so it ships in the public repo.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace plectro {

// Warm, dark wood palette shared by the editors.
namespace palette {
inline const juce::Colour background { 0xff1e1f24 };
inline const juce::Colour panel { 0xff2a2c33 };
inline const juce::Colour section { 0xff313440 }; // box fill, clearly above the background
inline const juce::Colour accent { 0xffd9a066 };   // warm wood tone
inline const juce::Colour text { 0xffe6e6e6 };
inline const juce::Colour dim { 0xff9aa0a6 };
} // namespace palette

// A dark, wood-toned look, a step up from the generic parameter panel.
class HumanLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HumanLookAndFeel();
    // Square, opaque tooltip (the default rounded fill left white triangles in the corners).
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    // A bit taller than the default so the text has breathing room, especially at the bottom.
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
};

// Same theme but with a large glyph font, for the round Reset button.
class GlyphLookAndFeel : public HumanLookAndFeel
{
public:
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jmax(20.0f, static_cast<float>(buttonHeight) * 0.92f)));
    }
};

// A small round button with a tiny glyph, used for the "clear the custom font" cross.
class RoundButtonLookAndFeel : public HumanLookAndFeel
{
public:
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isOver, bool isDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(juce::FontOptions(10.0f));
    }
};

// Three retro knob looks, applied to different rows so they can be compared side by side.
class KnobVintage : public HumanLookAndFeel   // dark cap + pointer line + surrounding ticks
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
};
class KnobArrow : public HumanLookAndFeel     // chicken-head arrow pointer
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
};
class KnobBakelite : public HumanLookAndFeel  // value arc + rim indicator dot
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
};

// A small round indicator that glows with a 0..1 level. Shows which tremolo mechanism (host
// keyswitch vs the rhythmic detector) is currently in action.
class TremoloLed : public juce::Component
{
public:
    juce::Colour colour { 0xffd9a066 };
    void setLevel(float l) { if (l != level_) { level_ = l; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    float level_ = 0.0f;
};

// Horizontal output level meter (0..1), left-to-right fill with a peak-hold feel.
class LevelMeter : public juce::Component
{
public:
    void setLevel(float l) { if (l != level_) { level_ = l; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    float level_ = 0.0f;
};

// Bipolar indicator (-1..1) centred at 0: fills right for positive, left for negative. Used to
// show the direction and amount the compression is shifting the current note's dynamic.
class BipolarMeter : public juce::Component
{
public:
    void setValue(float v) { if (v != value_) { value_ = v; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    float value_ = 0.0f;
};

} // namespace plectro
