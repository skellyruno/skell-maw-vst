// Chapter 5 - Shedding: Shed Skin (freeze / granular glitch)
#include "MawProcessorBase.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Shed Skin"; p.chapter = "Chapter 5 - Shedding"; p.tagline = "Leave a piece of yourself behind.";
            p.accent = 0xff6fd13a;
            p.crop = { 0.45f, 0.25f, 0.55f, 0.55f };
            p.params = {
                ParamSpec::flt ("size",    "Size",    20.0f, 500.0f, 120.0f, "ms", 0.5f, 1.0f),
                ParamSpec::flt ("pitch",   "Pitch",   -12.0f, 12.0f, 0.0f, "st", 1.0f, 0.1f),
                ParamSpec::flt ("scatter", "Scatter", 0.0f, 1.0f, 0.2f, "%"),
                ParamSpec::flt ("glitch",  "Glitch",  0.0f, 1.0f, 0.15f, "%"),
                ParamSpec::flt ("mix",     "Mix",     0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::toggle ("freeze", "FREEZE", false) };
            p.heroId = "size"; p.heroRect = { 0.60f, 0.30f, 0.38f, 0.45f };
            p.heroAxis = 0; p.heroSign = 1;
            p.clickToggleId = "freeze";                // click the serpent to shed
            p.glowPos = { 0.78f, 0.45f }; p.glowRadius = 0.15f;
            return p;
        }();
        return s;
    }

    class ShedSkin final : public MawProcessorBase
    {
    public:
        ShedSkin() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int, int) override
        {
            ringSize = (int) (2.0 * sampleRate) + 8;
            for (auto& r : ring) r.assign ((size_t) ringSize, 0.0f);
            wp = 0; wasFrozen = false; freezeMix = 0.0f; segStart = 0; segLen = 1;
            fadeCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = juce::jmin (2, buffer.getNumChannels());
            const bool frozen = getBool ("freeze");
            const float sizeSamples = getParam ("size") * 0.001f * (float) sr;
            const float rate = std::pow (2.0f, getParam ("pitch") / 12.0f);
            const float scatter = getParam ("scatter"), glitch = getParam ("glitch"), mix = getParam ("mix");

            if (frozen && ! wasFrozen)
            {
                segLen = juce::jlimit (256, ringSize - 2, (int) (sizeSamples * 3.0f));
                segStart = (wp - segLen + ringSize) % ringSize;
                newGrain (grains[0], sizeSamples, rate, scatter, glitch);
                newGrain (grains[1], sizeSamples, rate, scatter, glitch);
                grains[1].t = grains[0].len * 0.5f;
            }
            wasFrozen = frozen;

            for (int i = 0; i < n; ++i)
            {
                freezeMix += ((frozen ? 1.0f : 0.0f) - freezeMix) * fadeCoef;
                float wet[2] = { 0.0f, 0.0f };

                if (freezeMix > 0.0005f)
                {
                    for (auto& gr : grains)
                    {
                        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * gr.t / gr.len);
                        float off = std::fmod (gr.p0 + gr.t * gr.rate, (float) segLen);
                        if (off < 0.0f) off += (float) segLen;
                        for (int c = 0; c < nCh; ++c)
                            wet[c] += w * readRing (c, (float) segStart + off);

                        gr.t += 1.0f;
                        if (gr.t >= gr.len) newGrain (gr, sizeSamples, rate, scatter, glitch);
                    }
                }

                const float wetAmt = mix * freezeMix;
                for (int c = 0; c < nCh; ++c)
                {
                    float* d = buffer.getWritePointer (c);
                    const float in = d[i];
                    if (! frozen) ring[(size_t) c][(size_t) wp] = in;
                    d[i] = in * (1.0f - wetAmt) + wet[c] * wetAmt;
                }
                if (! frozen && ++wp >= ringSize) wp = 0;
            }
            reaction.store (freezeMix);
        }

    private:
        struct Grain { float t = 0.0f, len = 1024.0f, p0 = 0.0f, rate = 1.0f; };

        float readRing (int c, float pos) const
        {
            float p = std::fmod (pos, (float) ringSize);
            if (p < 0.0f) p += (float) ringSize;
            const int i0 = (int) p;
            const float f = p - (float) i0;
            const int i1 = (i0 + 1 >= ringSize) ? 0 : i0 + 1;
            const auto& r = ring[(size_t) c];
            return r[(size_t) i0] + f * (r[(size_t) i1] - r[(size_t) i0]);
        }

        void newGrain (Grain& g, float sizeSamples, float rate, float scatter, float glitch)
        {
            const bool glitchy = rng.nextFloat() < glitch;
            g.len = juce::jmax (64.0f, (glitchy && rng.nextBool()) ? sizeSamples * 0.5f : sizeSamples);
            g.rate = rate * ((glitchy && rng.nextBool()) ? -1.0f : 1.0f);
            g.p0 = rng.nextFloat() * scatter * (float) segLen;
            g.t = 0.0f;
        }

        std::vector<float> ring[2];
        int ringSize = 0, wp = 0, segStart = 0, segLen = 1;
        bool wasFrozen = false;
        float freezeMix = 0.0f, fadeCoef = 0.01f;
        Grain grains[2];
        juce::Random rng;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ShedSkin(); }
