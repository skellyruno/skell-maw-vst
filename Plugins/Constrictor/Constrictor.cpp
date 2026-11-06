// Chapter 6 - Truce: Constrictor (compressor with soft/hard ceiling)
#include "MawProcessorBase.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Constrictor"; p.chapter = "Chapter 6 - Truce"; p.tagline = "Hold tight. Don't panic.";
            p.accent = 0xffc99a52;
            p.crop = { 0.0f, 0.4f, 0.6f, 0.6f };
            p.params = {
                ParamSpec::flt ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB", 1.0f, 0.1f),
                ParamSpec::flt ("ratio",     "Ratio",     1.0f, 20.0f, 4.0f, "x", 0.5f, 0.1f),
                ParamSpec::flt ("attack",    "Attack",    0.1f, 100.0f, 10.0f, "ms", 0.4f, 0.1f),
                ParamSpec::flt ("release",   "Release",   10.0f, 1000.0f, 150.0f, "ms", 0.4f, 1.0f),
                ParamSpec::flt ("makeup",    "Makeup",    0.0f, 24.0f, 0.0f, "dB", 1.0f, 0.1f),
                ParamSpec::flt ("mix",       "Mix",       0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::choice ("knot", "Knot", { "Off", "Soft", "Hard" }, 1) };
            p.heroId = "threshold"; p.heroRect = { 0.0f, 0.62f, 0.20f, 0.36f };
            p.heroAxis = 1; p.heroSign = 1;            // pull the rope down to lower the threshold
            p.glowPos = { 0.10f, 0.78f }; p.glowRadius = 0.18f;
            return p;
        }();
        return s;
    }

    class Constrictor final : public MawProcessorBase
    {
    public:
        Constrictor() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double, int, int) override { grState = 0.0f; }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = buffer.getNumChannels();
            const float thr = getParam ("threshold"), ratio = getParam ("ratio");
            const float makeup = getParam ("makeup"), mix = getParam ("mix");
            const int knot = getChoice ("knot");
            const float atkC = std::exp (-1.0f / juce::jmax (1.0f, getParam ("attack") * 0.001f * (float) sr));
            const float relC = std::exp (-1.0f / juce::jmax (1.0f, getParam ("release") * 0.001f * (float) sr));
            constexpr float knee = 6.0f, ceiling = 0.97f;
            const float slope = 1.0f - 1.0f / ratio;
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                float det = 0.0f;
                for (int c = 0; c < nCh; ++c) det = juce::jmax (det, std::abs (buffer.getSample (c, i)));
                const float over = juce::Decibels::gainToDecibels (det, -100.0f) - thr;

                float gr = 0.0f;                                   // gain reduction in dB (>= 0)
                if (2.0f * over > knee)               gr = slope * over;
                else if (2.0f * over > -knee)         gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);

                const float coef = gr > grState ? atkC : relC;
                grState = coef * grState + (1.0f - coef) * gr;
                maxGr = juce::jmax (maxGr, grState);

                const float gain = juce::Decibels::decibelsToGain (makeup - grState);
                for (int c = 0; c < nCh; ++c)
                {
                    float* d = buffer.getWritePointer (c);
                    const float dry = d[i];
                    float y = dry * gain;
                    if (knot == 1)      y = ceiling * std::tanh (y / ceiling);
                    else if (knot == 2) y = juce::jlimit (-ceiling, ceiling, y);
                    d[i] = dry * (1.0f - mix) + y * mix;
                }
            }
            reaction.store (juce::jlimit (0.0f, 1.0f, maxGr / 18.0f));   // the rope strains with gain reduction
        }

    private:
        float grState = 0.0f;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Constrictor(); }
