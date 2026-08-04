#include "EditorLookAndFeel.h"

namespace plectro {

using palette::accent;
using palette::background;
using palette::dim;
using palette::panel;
using palette::section;
using palette::text;

HumanLookAndFeel::HumanLookAndFeel()
    : juce::LookAndFeel_V4(juce::LookAndFeel_V4::getMidnightColourScheme())
{
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, panel);
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, panel);
    setColour(juce::ComboBox::arrowColourId, accent);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::PopupMenu::highlightedTextColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::textColourOffId, text);
    // Tooltips get a warm, lighter box with an accent border so they clearly read as a tooltip
    // against the cool dark panels.
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff4a453a));
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, accent);
}

void HumanLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& tip, int width, int height)
{
    g.fillAll(findColour(juce::TooltipWindow::backgroundColourId)); // square fill, no rounded corners
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRect(0, 0, width, height, 1);
    g.setColour(findColour(juce::TooltipWindow::textColourId));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText(tip, 8, 6, width - 16, height - 14, juce::Justification::topLeft, 10);
}

juce::Rectangle<int> HumanLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    // Start from the default bounds, then add 10px of padding. Crucially, extend it AWAY from the
    // cursor: the default places the box below the mouse for controls in the top half and above it
    // for controls in the bottom half, always leaving a small gap. Extending downward in both cases
    // would make the padded box reach back over the cursor for bottom-half controls, so the tooltip
    // window itself becomes the component under the mouse, which makes JUCE reset and hide it in a
    // loop, and the tip never settles. Padding upward when the box sits above the cursor keeps the
    // gap intact.
    auto b = juce::LookAndFeel_V2::getTooltipBounds(text, screenPos, parentArea);
    const bool aboveCursor = b.getBottom() <= screenPos.y;
    b = aboveCursor ? b.withTop(b.getY() - 10) : b.withHeight(b.getHeight() + 10);
    return b.constrainedWithin(parentArea);
}

void HumanLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                                        bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    const bool on = b.getToggleState();
    const float ea = b.isEnabled() ? 1.0f : 0.4f; // whole switch fades when disabled
    auto bounds = b.getLocalBounds().toFloat();

    // Pill switch on the left: a rounded track with a sliding circular knob. Subtler than the
    // default framed tick box; "on" fills the track with the accent, "off" is a dim panel outline.
    const float h = juce::jmin(12.0f, bounds.getHeight());
    const float w = h * 1.85f;
    juce::Rectangle<float> track(bounds.getX(), bounds.getCentreY() - h * 0.5f, w, h);

    g.setColour((on ? accent : panel).withMultipliedAlpha(ea));
    g.fillRoundedRectangle(track, h * 0.5f);
    if (!on)
    {
        g.setColour((shouldDrawButtonAsHighlighted ? dim.brighter(0.2f) : dim).withMultipliedAlpha(ea));
        g.drawRoundedRectangle(track.reduced(0.5f), h * 0.5f, 1.0f);
    }

    const float pad = 2.0f;
    const float knobD = h - pad * 2.0f;
    const float kx = on ? track.getRight() - knobD - pad : track.getX() + pad;
    g.setColour((on ? background : text.withAlpha(0.85f)).withMultipliedAlpha(ea));
    g.fillEllipse(kx, track.getY() + pad, knobD, knobD);

    // Label to the right of the switch.
    g.setColour(b.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(ea));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    const float textX = track.getRight() + 8.0f;
    g.drawText(b.getButtonText(),
               juce::Rectangle<float>(textX, bounds.getY(), bounds.getRight() - textX, bounds.getHeight()),
               juce::Justification::centredLeft, true);
}

void HumanLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool isOver, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const float ea = b.isEnabled() ? 1.0f : 0.4f; // fade when disabled
    const juce::Colour fill = (isDown  ? panel.brighter(0.28f)
                               : isOver ? panel.brighter(0.15f)
                                        : panel.brighter(0.05f)).withMultipliedAlpha(ea);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour((isOver ? accent : dim).withMultipliedAlpha(ea));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

void RoundButtonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                  bool isOver, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const juce::Colour c = isDown ? panel.brighter(0.25f) : isOver ? panel.brighter(0.12f) : panel;
    g.setColour(c);
    g.fillEllipse(r);
    g.setColour(dim);
    g.drawEllipse(r, 1.0f);
}

namespace {
struct KnobGeom
{
    float cx, cy, r, angle;
};
KnobGeom knobGeom(int x, int y, int w, int h, float pos, float a0, float a1, float margin)
{
    const float r = juce::jmin(w, h) * 0.5f - margin;
    return { x + w * 0.5f, y + h * 0.5f, r, a0 + pos * (a1 - a0) };
}
juce::Point<float> onDial(float cx, float cy, float radius, float angle)
{
    return { cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi),
             cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi) };
}
} // namespace

void KnobVintage::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float a0, float a1, juce::Slider& slider)
{
    const auto k = knobGeom(x, y, w, h, pos, a0, a1, 8.0f);

    // Ticks around the dial.
    g.setColour(dim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (i / 10.0f) * (a1 - a0);
        const auto p1 = onDial(k.cx, k.cy, k.r + 2.0f, a);
        const auto p2 = onDial(k.cx, k.cy, k.r + 6.0f, a);
        g.drawLine({ p1, p2 }, 1.4f);
    }

    // Cap.
    g.setColour(section.brighter(0.15f));
    g.fillEllipse(k.cx - k.r, k.cy - k.r, k.r * 2, k.r * 2);
    g.setColour(panel.brighter(0.2f));
    g.drawEllipse(k.cx - k.r, k.cy - k.r, k.r * 2, k.r * 2, 1.5f);

    // Pointer line.
    const auto tip = onDial(k.cx, k.cy, k.r - 3.0f, k.angle);
    const auto base = onDial(k.cx, k.cy, k.r * 0.25f, k.angle);
    g.setColour(accent);
    g.drawLine({ base, tip }, 3.0f);

    if (!slider.isEnabled()) { g.setColour(background.withAlpha(0.55f)); g.fillRect(x, y, w, h); }
}

void KnobArrow::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                 float a0, float a1, juce::Slider& slider)
{
    const auto k = knobGeom(x, y, w, h, pos, a0, a1, 8.0f);

    // Base cap.
    g.setColour(section.brighter(0.1f));
    g.fillEllipse(k.cx - k.r * 0.7f, k.cy - k.r * 0.7f, k.r * 1.4f, k.r * 1.4f);
    g.setColour(panel.brighter(0.2f));
    g.drawEllipse(k.cx - k.r * 0.7f, k.cy - k.r * 0.7f, k.r * 1.4f, k.r * 1.4f, 1.5f);

    // Chicken-head arrow pointer, rotated to the value angle.
    juce::Path arrow;
    const float half = k.r * 0.22f;
    arrow.startNewSubPath(0.0f, -k.r + 2.0f);       // tip (up)
    arrow.lineTo(-half, k.r * 0.35f);
    arrow.lineTo(half, k.r * 0.35f);
    arrow.closeSubPath();
    g.setColour(accent);
    g.fillPath(arrow, juce::AffineTransform::rotation(k.angle).translated(k.cx, k.cy));

    if (!slider.isEnabled()) { g.setColour(background.withAlpha(0.55f)); g.fillRect(x, y, w, h); }
}

void KnobBakelite::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float a0, float a1, juce::Slider& slider)
{
    const auto k = knobGeom(x, y, w, h, pos, a0, a1, 8.0f);
    const float arcR = k.r;

    juce::Path back;
    back.addCentredArc(k.cx, k.cy, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour(panel);
    g.strokePath(back, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc(k.cx, k.cy, arcR, arcR, 0.0f, a0, k.angle, true);
    g.setColour(accent);
    g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bakelite cap.
    const float cr = k.r * 0.72f;
    g.setColour(section.darker(0.2f));
    g.fillEllipse(k.cx - cr, k.cy - cr, cr * 2, cr * 2);
    g.setColour(panel);
    g.drawEllipse(k.cx - cr, k.cy - cr, cr * 2, cr * 2, 1.0f);

    // Rim indicator dot.
    const auto dot = onDial(k.cx, k.cy, cr - 6.0f, k.angle);
    g.setColour(accent);
    g.fillEllipse(dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);

    if (!slider.isEnabled()) { g.setColour(background.withAlpha(0.55f)); g.fillRect(x, y, w, h); }
}

void TremoloLed::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const juce::Colour off { 0xff2a2c33 };
    g.setColour(off.interpolatedWith(colour, level_));
    g.fillEllipse(r);
    if (level_ > 0.01f) {
        g.setColour(colour.withAlpha(0.35f * level_));
        g.drawEllipse(r.expanded(1.5f), 2.0f);
    }
    g.setColour(juce::Colour(0xff45474f));
    g.drawEllipse(r, 1.0f);
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1e1f24));
    g.fillRoundedRectangle(r, 2.0f);
    const float lvl = juce::jlimit(0.0f, 1.0f, level_);
    const juce::Colour c = barColour_.getAlpha() > 0
                         ? barColour_
                         : (lvl < 0.7f ? juce::Colour(0xff6fcf6f)
                            : lvl < 0.9f ? juce::Colour(0xffd9a066)
                                         : juce::Colour(0xffd96666));
    g.setColour(c);
    if (fillFromRight_)
        g.fillRoundedRectangle(r.withTrimmedLeft(r.getWidth() * (1.0f - lvl)), 2.0f); // grows right -> left
    else
        g.fillRoundedRectangle(r.withWidth(r.getWidth() * lvl), 2.0f);                // grows left -> right
    g.setColour(juce::Colour(0xff45474f));
    g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
}


} // namespace plectro
