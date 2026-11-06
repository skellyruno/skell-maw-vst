#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    inline const juce::Colour text   { 0xffe8d9c0 };
    inline const juce::Colour muted  { 0xffa89a88 };
    inline const juce::Colour ink    { 0xff0b0709 };

    // Uses the host's system sans family; no bundled font assets.
    inline juce::Font font (float px, bool bold = false, bool italic = false)
    {
        juce::String style = bold ? (italic ? "Bold Italic" : "Bold")
                                  : (italic ? "Italic" : "Regular");
        return juce::Font (juce::FontOptions (px).withStyle (style));
    }

    constexpr float pi = juce::MathConstants<float>::pi;
}
