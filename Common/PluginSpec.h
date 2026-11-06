#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Describes one parameter. The shared processor base builds the APVTS layout
// from these and the shared editor builds its controls from them.
struct ParamSpec
{
    enum class Kind { Float, Choice, Bool };

    juce::String id, name, unit;
    Kind kind = Kind::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f, step = 0.001f, skew = 1.0f;
    juce::StringArray choices;
    int defChoice = 0;
    bool defBool = false;

    // unit: "%" (value is 0..1), "Hz", "ms", "dB", "st" or ""
    static ParamSpec flt (juce::String id, juce::String name, float mn, float mx, float def,
                          juce::String unit = {}, float skew = 1.0f, float step = 0.001f)
    {
        ParamSpec p;
        p.id = id; p.name = name; p.min = mn; p.max = mx; p.def = def;
        p.unit = unit; p.skew = skew; p.step = step;
        return p;
    }
    static ParamSpec choice (juce::String id, juce::String name, juce::StringArray items, int def = 0)
    {
        ParamSpec p;
        p.id = id; p.name = name; p.kind = Kind::Choice; p.choices = items; p.defChoice = def;
        return p;
    }
    static ParamSpec toggle (juce::String id, juce::String name, bool def = false)
    {
        ParamSpec p;
        p.id = id; p.name = name; p.kind = Kind::Bool; p.defBool = def;
        return p;
    }
};

// Describes one plugin: its look, its controls, and the "hero" gesture on the art.
// All art coordinates are fractions (0..1) of the ORIGINAL illustration.
struct PluginSpec
{
    juce::String name, chapter, tagline, version { "1.0.0" };
    juce::uint32 accent = 0xffd8341f;

    juce::Rectangle<float> crop { 0.0f, 0.0f, 1.0f, 1.0f };   // part of the art shown in the window
    std::vector<ParamSpec> params;

    juce::String heroId;                                       // parameter driven by dragging on the art
    juce::Rectangle<float> heroRect;                           // draggable hotspot
    int heroAxis = 0;                                          // 0 = horizontal drag, 1 = vertical drag
    int heroSign = 1;                                          // +1: right/up increases, -1: left/down increases
    juce::Point<float> glowPos { 0.5f, 0.5f };                 // centre of the reactive glow
    float glowRadius = 0.25f;
    juce::String clickToggleId;                                // optional bool toggled by clicking the hotspot
};
