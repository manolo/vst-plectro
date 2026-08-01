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
    // Toggles render as a modern pill switch (sliding knob) instead of the default prominent tick box.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    // Text buttons get a filled, outlined rounded body so they read as buttons at rest, not only on
    // hover (the default fill blends into the panels).
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isOver, bool isDown) override;
};

// Same theme but with a large glyph font, for the Reset button: just the glyph, no box around it.
class GlyphLookAndFeel : public HumanLookAndFeel
{
public:
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jmax(22.0f, static_cast<float>(buttonHeight) * 1.01f)));
    }
    // No framed box: only a faint round highlight on hover/press for affordance.
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool isOver, bool isDown) override
    {
        if (isOver || isDown)
        {
            g.setColour(juce::Colours::white.withAlpha(isDown ? 0.12f : 0.07f));
            g.fillEllipse(b.getLocalBounds().toFloat().reduced(1.0f));
        }
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
    void setColour(juce::Colour c) { if (c != colour) { colour = c; repaint(); } }
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
    // Fixed bar colour. When left transparent (the default) the bar ramps green -> amber -> red with
    // level, which suits the output VU; a solid colour suits a labelled meter (compression, exciter).
    void setBarColour(juce::Colour c) { barColour_ = c; }
    // Fill from the right edge growing left instead of the default left edge growing right. Used so a
    // pair of meters can grow outward from a shared centre.
    void setFillFromRight(bool r) { fillFromRight_ = r; }
    void paint(juce::Graphics&) override;
private:
    float level_ = 0.0f;
    juce::Colour barColour_ {};
    bool fillFromRight_ = false;
};

} // namespace plectro
