#pragma once
#include "juce_shim.h"
struct ParamSpec {
    enum class Kind { Float, Choice, Bool };
    juce::String id, name, unit; Kind kind = Kind::Float; float def = 0; int defChoice = 0; bool defBool = false;
    static ParamSpec flt (juce::String id, juce::String /*name*/, float, float, float def, juce::String = {}, float = 1, float = 0) { ParamSpec p; p.id = id; p.def = def; return p; }
    static ParamSpec choice (juce::String id, juce::String, juce::StringArray, int def = 0) { ParamSpec p; p.id = id; p.kind = Kind::Choice; p.def = (float) def; return p; }
    static ParamSpec toggle (juce::String id, juce::String, bool def = false) { ParamSpec p; p.id = id; p.kind = Kind::Bool; p.def = def ? 1.f : 0.f; return p; }
};
struct PluginSpec {
    juce::String name, chapter, tagline; juce::uint32 accent = 0; juce::Rectangle<float> crop, heroRect; std::vector<ParamSpec> params;
    juce::String heroId, clickToggleId; int heroAxis = 0, heroSign = 1; enum Fx { fxFireflies, fxEmbers, fxPetals, fxFlecks, fxButterflies }; int fx = 0; bool rings = false, eye = false, tongue = false, fog = false, sparkles = false, squeeze = false, frost = false; int echoes = 0; juce::Point<float> glowPos; float glowRadius = 0;
};
