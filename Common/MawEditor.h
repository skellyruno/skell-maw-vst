#pragma once
#include <array>
#include "MawProcessorBase.h"
#include "MawLookAndFeel.h"

// One editor for the whole collection. It draws a crop of the illustration, builds
// knobs / dropdowns / toggles from the PluginSpec, and turns the "hero" hotspot on the
// art into a draggable control.
class MawEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit MawEditor (MawProcessorBase&);
    ~MawEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob   { std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::Label> label; std::unique_ptr<SliderAtt> att; };
    struct Combo  { std::unique_ptr<juce::ComboBox> box; std::unique_ptr<ComboAtt> att; };
    struct Toggle { std::unique_ptr<juce::ToggleButton> button; std::unique_ptr<ButtonAtt> att; };

    void timerCallback() override;
    void rebuildArtCache (int physicalSize);
    void updateMotes (float dt);
    float energyNow() const;
    void drawEye (juce::Graphics&, const juce::AffineTransform& artToLocal, float pulse) const;
    void drawSparkles (juce::Graphics&, const juce::AffineTransform& artToLocal, float strength) const;
    void drawFlies (juce::Graphics&, const juce::AffineTransform& artToLocal, float energy) const;
    void drawSprite (juce::Graphics&, int sprite, const juce::AffineTransform& place, float heading, float flap) const;
    void drawMotes (juce::Graphics&, float W, float H, juce::Colour accent, float energy) const;
    void popupMainMenu();
    juce::Point<float> toWindow (juce::Point<float> p) const;
    juce::Rectangle<float> toWindow (juce::Rectangle<float> r) const;
    juce::Rectangle<float> heroWindowRect() const;

    MawProcessorBase& proc;
    const PluginSpec& spec;
    MawLookAndFeel look;
    juce::Image art, artCache, grain, tongueImg;   // artCache: the crop pre-scaled to this window's pixel size
    juce::Rectangle<int> artCrop;                  // the part of art that artCache holds, in art pixels
    int artCacheSize = 0;
    struct Mote { float x = 0, y = 0, a = 0, v = 0, phase = 0, size = 0; int sprite = 0; };
    struct Fly  { float flap = 0, path = 0, seed = 0, rate = 1; };   // one per butterfly cut out of the art
    std::array<Fly, 6> flies;
    std::array<juce::Image, 6> sprites;
    void spawnMote (Mote&, bool anywhere);
    std::vector<Mote> motes;
    juce::Random rng { 42 };
    double lastTick = 0.0;
    float t = 0.0f, ringPhase = 0.0f, squeezeSm = 0.0f, flickT = 0.0f, nextFlick = 1.5f, tongueFlick = 0.0f;
    juce::Rectangle<int> sq;   // centred square the UI is laid out in (hosts may ignore the aspect lock)

    juce::RangedAudioParameter* hero = nullptr;
    float heroValue = 0.0f, heroStart = 0.0f, level = 0.0f;
    bool hovering = false, dragging = false, moved = false;

    std::vector<Knob> knobs;
    std::vector<Combo> combos;
    std::vector<Toggle> toggles;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MawEditor)
};
