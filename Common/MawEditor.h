#pragma once
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
    void popupMainMenu();
    juce::Point<float> toWindow (juce::Point<float> p) const;
    juce::Rectangle<float> toWindow (juce::Rectangle<float> r) const;
    juce::Rectangle<float> heroWindowRect() const;

    MawProcessorBase& proc;
    const PluginSpec& spec;
    MawLookAndFeel look;
    juce::Image art;

    juce::RangedAudioParameter* hero = nullptr;
    float heroValue = 0.0f, heroStart = 0.0f, level = 0.0f;
    bool hovering = false, dragging = false, moved = false;

    std::vector<Knob> knobs;
    std::vector<Combo> combos;
    std::vector<Toggle> toggles;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MawEditor)
};
