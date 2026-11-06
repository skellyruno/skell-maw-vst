#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginSpec.h"

// Shared base for every plugin in the collection. A plugin only has to supply a
// PluginSpec plus prepare() and process().
class MawProcessorBase : public juce::AudioProcessor
{
public:
    explicit MawProcessorBase (const PluginSpec& s);
    ~MawProcessorBase() override = default;

    const PluginSpec& getSpec() const noexcept { return spec; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return spec.name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor to animate the art (0..1-ish).
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> reaction   { 0.0f };   // plugin-specific (gain reduction, LFO, freeze...)

protected:
    virtual void prepare (double sampleRate, int maxBlock, int numChannels) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;

    float getParam  (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    int   getChoice (const juce::String& id) const { return juce::roundToInt (getParam (id)); }
    bool  getBool   (const juce::String& id) const { return getParam (id) > 0.5f; }
    double getHostBpm() const;

    double sr = 44100.0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (const PluginSpec&);

    const PluginSpec& spec;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MawProcessorBase)
};
