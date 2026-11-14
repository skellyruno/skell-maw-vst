#pragma once
#include "PluginSpec.h"
#include "DspUtil.h"
class MawProcessorBase : public juce::AudioProcessor {
public:
    explicit MawProcessorBase (const PluginSpec& s) : spec (s) { for (auto& p : s.params) values[p.id] = p.def; }
    void set (const char* id, float v) { values[id] = v; }
    void prepareToPlay (double s, int blk) { sr = s; reaction = 0; prepare (s, blk, 2); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { process (b); }
    std::atomic<float> reaction { 0.0f };
protected:
    virtual void prepare (double, int, int) = 0;
    virtual void process (juce::AudioBuffer<float>&) = 0;
    float getParam (const juce::String& id) const { return values.at (id); }
    int getChoice (const juce::String& id) const { return juce::roundToInt (getParam (id)); }
    bool getBool (const juce::String& id) const { return getParam (id) > 0.5f; }
    double getHostBpm() const { return 120.0; }
    double sr = 44100.0;
    void setLatencySamples (int) {}
private:
    const PluginSpec& spec; std::map<juce::String, float> values;
};
