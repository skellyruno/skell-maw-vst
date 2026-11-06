// Chapter 1 - Meeting: Dark Forest (reverb)
#include "MawProcessorBase.h"
#include "DspUtil.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Dark Forest"; p.chapter = "Chapter 1 - Meeting"; p.tagline = "Don't wander far.";
            p.accent = 0xff5fae6b;
            p.crop = { 0.0f, 0.0f, 1.0f, 1.0f };
            p.params = {
                ParamSpec::flt ("size",    "Size",     0.0f, 1.0f, 0.55f, "%"),
                ParamSpec::flt ("damping", "Damping",  0.0f, 1.0f, 0.45f, "%"),
                ParamSpec::flt ("predelay","Pre-delay",0.0f, 200.0f, 20.0f, "ms", 0.5f, 0.1f),
                ParamSpec::flt ("width",   "Width",    0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::flt ("tone",    "High cut", 1000.0f, 18000.0f, 9000.0f, "Hz", 0.35f, 1.0f),
                ParamSpec::flt ("mix",     "Mix",      0.0f, 1.0f, 0.3f, "%") };
            p.heroId = "size"; p.heroRect = { 0.28f, 0.0f, 0.30f, 0.62f };
            p.heroAxis = 1; p.heroSign = 1;
            p.glowPos = { 0.5f, 0.4f }; p.glowRadius = 0.35f;
            return p;
        }();
        return s;
    }

    class DarkForest final : public MawProcessorBase
    {
    public:
        DarkForest() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int maxBlock, int) override
        {
            rev.setSampleRate (sampleRate);
            rev.reset();
            for (auto& d : pre) d.init ((int) (0.25 * sampleRate) + 4);
            for (auto& w : wet) w.assign ((size_t) maxBlock, 0.0f);
            for (auto& l : lp) l.reset();
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = juce::jmin (2, buffer.getNumChannels());
            if (n > (int) wet[0].size()) return;   // host exceeded declared block size

            juce::Reverb::Parameters rp;
            rp.roomSize = getParam ("size");  rp.damping = getParam ("damping");
            rp.wetLevel = 1.0f;  rp.dryLevel = 0.0f;  rp.width = getParam ("width");  rp.freezeMode = 0.0f;
            rev.setParameters (rp);

            const float preSamples = juce::jmax (1.0f, getParam ("predelay") * 0.001f * (float) sr);
            const float mix = getParam ("mix");
            const float a = maw::OnePoleLP::coeff (getParam ("tone"), sr);

            for (int c = 0; c < nCh; ++c)
            {
                const float* in = buffer.getReadPointer (c);
                for (int i = 0; i < n; ++i)
                {
                    pre[(size_t) c].push (in[i]);
                    wet[(size_t) c][(size_t) i] = pre[(size_t) c].read (preSamples);
                }
            }

            if (nCh == 2) rev.processStereo (wet[0].data(), wet[1].data(), n);
            else          rev.processMono (wet[0].data(), n);

            float peak = 0.0f;
            for (int c = 0; c < nCh; ++c)
            {
                float* d = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float w = lp[(size_t) c].process (wet[(size_t) c][(size_t) i], a);
                    peak = juce::jmax (peak, std::abs (w));
                    d[i] = d[i] * (1.0f - mix) + w * mix;
                }
            }
            reaction.store (juce::jlimit (0.0f, 1.0f, peak * 2.0f));
        }

    private:
        juce::Reverb rev;
        maw::DelayLine pre[2];
        std::vector<float> wet[2];
        maw::OnePoleLP lp[2];
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DarkForest(); }
