#include "MawProcessorBase.h"
#include "MawEditor.h"

namespace
{
    juce::String formatValue (const juce::String& unit, float v)
    {
        if (unit == "%")  return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                              : juce::String (juce::roundToInt (v)) + " Hz";
        if (unit == "ms") return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms";
        if (unit == "dB") return juce::String (v, 1) + " dB";
        if (unit == "st") return juce::String (v, 1) + " st";
        if (unit == "x")  return juce::String (v, 1) + " : 1";
        return juce::String (v, 2);
    }

    float parseValue (const juce::String& unit, const juce::String& t)
    {
        const float v = t.retainCharacters ("0123456789.-").getFloatValue();
        if (unit == "%") return v / 100.0f;
        if (unit == "Hz" && t.containsIgnoreCase ("k")) return v * 1000.0f;
        return v;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MawProcessorBase::makeLayout (const PluginSpec& s)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& p : s.params)
    {
        switch (p.kind)
        {
            case ParamSpec::Kind::Float:
            {
                const auto unit = p.unit;
                auto attrs = AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })
                    .withValueFromStringFunction ([unit] (const String& t) { return parseValue (unit, t); });
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { p.id, 1 }, p.name,
                    NormalisableRange<float> (p.min, p.max, p.step, p.skew), p.def, attrs));
                break;
            }
            case ParamSpec::Kind::Choice:
                layout.add (std::make_unique<AudioParameterChoice> (
                    ParameterID { p.id, 1 }, p.name, p.choices, p.defChoice));
                break;
            case ParamSpec::Kind::Bool:
                layout.add (std::make_unique<AudioParameterBool> (
                    ParameterID { p.id, 1 }, p.name, p.defBool));
                break;
        }
    }
    return layout;
}

MawProcessorBase::MawProcessorBase (const PluginSpec& s)
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", makeLayout (s)),
      spec (s)
{
}

bool MawProcessorBase::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void MawProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    inputLevel.store (0.0f);
    reaction.store (0.0f);
    prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void MawProcessorBase::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    const float pk = buffer.getMagnitude (0, n);
    const float cur = inputLevel.load();
    inputLevel.store (pk > cur ? pk : cur * 0.92f + pk * 0.08f);

    process (buffer);
}

double MawProcessorBase::getHostBpm() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                return juce::jlimit (30.0, 300.0, *bpm);
    return 120.0;
}

juce::AudioProcessorEditor* MawProcessorBase::createEditor()
{
    return new MawEditor (*this);
}

void MawProcessorBase::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MawProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
