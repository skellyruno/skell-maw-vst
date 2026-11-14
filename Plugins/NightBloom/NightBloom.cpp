// Chapter 7 - Bloom: Night Bloom (harmonic exciter)
#include "MawProcessorBase.h"
#include "DspUtil.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Night Bloom"; p.chapter = "Chapter 7 - Bloom"; p.tagline = "Everything opens eventually.";
            p.accent = 0xffee8fb0;
            p.crop = { 0.40f, 0.0f, 0.60f, 0.60f };
            p.params = {
                ParamSpec::flt ("amount", "Bloom",  0.0f, 1.0f, 0.4f, "%"),
                ParamSpec::flt ("freq",   "Above",  1000.0f, 12000.0f, 4000.0f, "Hz", 0.5f, 1.0f),
                ParamSpec::flt ("drive",  "Drive",  0.0f, 24.0f, 12.0f, "dB", 1.0f, 0.1f),
                ParamSpec::flt ("mix",    "Mix",    0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::flt ("output", "Output", -12.0f, 12.0f, 0.0f, "dB", 1.0f, 0.1f) };
            p.heroId = "amount"; p.heroRect = { 0.78f, 0.0f, 0.22f, 0.14f };
            p.heroAxis = 1; p.heroSign = 1;
            p.glowPos = { 0.90f, 0.05f }; p.glowRadius = 0.15f;
            p.fx = PluginSpec::fxPetals;
            p.sparkles = true;
            return p;
        }();
        return s;
    }

    class NightBloom final : public MawProcessorBase
    {
    public:
        NightBloom() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int, int) override
        {
            for (auto& f : hpIn)  f.reset();
            for (auto& f : hpOut) f.reset();

            // Parameter automation must not step the harmonic layer or master gain.
            // Initialize from the restored state, so loading an existing preset adds no fade.
            bloomSm.reset (sampleRate, 0.01);
            outputSm.reset (sampleRate, 0.01);
            bloomSm.setCurrentAndTargetValue (getParam ("amount") * getParam ("mix") * 0.75f);
            outputSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (getParam ("output")));
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = juce::jmin (2, buffer.getNumChannels());
            const float freq = getParam ("freq");
            const float drive = juce::Decibels::decibelsToGain (getParam ("drive"));
            bloomSm.setTargetValue (getParam ("amount") * getParam ("mix") * 0.75f);
            outputSm.setTargetValue (juce::Decibels::decibelsToGain (getParam ("output")));
            float peak = 0.0f;

            for (int c = 0; c < nCh; ++c)
            {
                hpIn[c].setHighpass (sr, freq);
                hpOut[c].setHighpass (sr, freq);
            }

            // Advance the smoothers once per sample, not once per channel. Both channels
            // receive precisely the same automation trajectory and keep their stereo image.
            for (int i = 0; i < n; ++i)
            {
                const float bloom = bloomSm.getNextValue();
                const float gain = outputSm.getNextValue();
                for (int c = 0; c < nCh; ++c)
                {
                    float* d = buffer.getWritePointer (c);
                    const float x = d[i];
                    const float h = hpIn[c].process (x);
                    const float shaped = std::tanh (h * drive + 0.15f) - std::tanh (0.15f);
                    const float excited = hpOut[c].process (shaped);
                    peak = juce::jmax (peak, std::abs (excited));
                    d[i] = (x + bloom * excited) * gain;
                }
            }
            reaction.store (juce::jlimit (0.0f, 1.0f, peak * bloomSm.getCurrentValue() * 2.0f));
        }

    private:
        maw::Biquad hpIn[2], hpOut[2];
        juce::SmoothedValue<float> bloomSm, outputSm;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NightBloom(); }
