#include "MawLookAndFeel.h"

MawLookAndFeel::MawLookAndFeel (juce::Colour a) : accent (a)
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff17100f));
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xd9150c0e));
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, Theme::text);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Theme::text);
}

void MawLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = juce::jmax (3.0f, r * 0.14f);
    const float arcR = r - lw;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ang = startAngle + pos * (endAngle - startAngle);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, ang, true);
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float fr = arcR * 0.72f;
    g.setColour (juce::Colour (0xe6120b0d));
    g.fillEllipse (c.x - fr, c.y - fr, fr * 2.0f, fr * 2.0f);

    const float s = std::sin (ang), co = std::cos (ang);
    g.setColour (Theme::text);
    g.drawLine (c.x + fr * 0.30f * s, c.y - fr * 0.30f * co,
                c.x + fr * 0.88f * s, c.y - fr * 0.88f * co, lw * 0.6f);
}

void MawLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent.withAlpha (0.85f) : juce::Colour (0xd9150c0e));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (accent.withAlpha (highlighted ? 1.0f : 0.65f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.5f);
    g.setColour (on ? juce::Colours::black : Theme::text);
    g.setFont (Theme::font (14.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void MawLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff17100f));
    g.setColour (accent.withAlpha (0.4f));
    g.drawRect (0, 0, w, h, 1);
}

void MawLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                        bool separator, bool active, bool highlighted, bool ticked, bool,
                                        const juce::String& name, const juce::String& shortcut,
                                        const juce::Drawable*, const juce::Colour*)
{
    if (separator)
    {
        g.setColour (Theme::muted.withAlpha (0.25f));
        g.drawLine ((float) bounds.getX() + 10.0f, (float) bounds.getCentreY(),
                    (float) bounds.getRight() - 10.0f, (float) bounds.getCentreY());
        return;
    }
    if (highlighted && active)
    {
        g.setColour (accent.withAlpha (0.2f));
        g.fillRoundedRectangle (bounds.toFloat().reduced (2.0f), 5.0f);
    }
    g.setColour (active ? Theme::text : Theme::muted.withAlpha (0.5f));
    g.setFont (Theme::font (14.0f));
    g.drawText ((ticked ? juce::String::fromUTF8 ("\xe2\x9c\x93  ") : "   ") + name,
                bounds.reduced (6), juce::Justification::centredLeft);
    if (shortcut.isNotEmpty())
    {
        g.setColour (Theme::muted);
        g.drawText (shortcut, bounds.reduced (12), juce::Justification::centredRight);
    }
}
