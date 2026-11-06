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
            p.crop = { 0.50f, 0.0f, 0.50f, 0.50f };
            p.params = {
                ParamSpec::flt ("amount", "Bloom",  0.0f, 1.0f, 0.4f, "%"),
                ParamSpec::flt ("freq",   "Above",  1000.0f, 12000.0f, 4000.0f, "Hz", 0.5f, 1.0f),
                ParamSpec::flt ("drive",  "Drive",  0.0f, 24.0f, 12.0f, "dB", 1.0f, 0.1f),
                ParamSpec::flt ("mix",    "Mix",    0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::flt ("output", "Output", -12.0f, 12.0f, 0.0f, "dB", 1.0f, 0.1f) };
            p.heroId = "amount"; p.heroRect = { 0.78f, 0.0f, 0.22f, 0.14f };
            p.heroAxis = 1; p.heroSign = 1;
            p.glowPos = { 0.90f, 0.05f }; p.glowRadius = 0.15f;
            return p;
        }();
        return s;
    }

    class NightBloom final : public MawProcessorBase
    {
    public:
        NightBloom() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double, int, int) override
        {
            for (auto& f : hpIn)  f.reset();
            for (auto& f : hpOut) f.reset();
            lastOut = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = juce::jmin (2, buffer.getNumChannels());
            const float freq = getParam ("freq"), amount = getParam ("amount") * 0.75f;
            const float g = juce::Decibels::decibelsToGain (getParam ("drive")), mix = getParam ("mix");
            const float outGain = juce::Decibels::decibelsToGain (getParam ("output"));
            float peak = 0.0f;

            for (int c = 0; c < nCh; ++c)
            {
                hpIn[c].setHighpass (sr, freq);
                hpOut[c].setHighpass (sr, freq);
                float* d = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float x = d[i];
                    const float h = hpIn[c].process (x);
                    // asymmetric shaper adds even + odd harmonics; the second filter removes any low-end it creates
                    float e = std::tanh (h * g + 0.15f) - std::tanh (0.15f);
                    e = hpOut[c].process (e);
                    peak = juce::jmax (peak, std::abs (e));
                    const float wet = x + amount * e;
                    d[i] = x * (1.0f - mix) + wet * mix;
                }
            }
            buffer.applyGainRamp (0, n, lastOut, outGain);
            lastOut = outGain;
            reaction.store (juce::jlimit (0.0f, 1.0f, peak * amount * 2.0f));
        }

    private:
        maw::Biquad hpIn[2], hpOut[2];
        float lastOut = 1.0f;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NightBloom(); }
