// Chapter 4 - The Swarm: Butterfly Delay (ping-pong delay with flutter)
#include "MawProcessorBase.h"
#include "DspUtil.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Butterfly Delay"; p.chapter = "Chapter 4 - The Swarm"; p.tagline = "They never land.";
            p.accent = 0xffe58fa8;
            p.crop = { 0.0f, 0.0f, 0.7f, 0.7f };
            p.params = {
                ParamSpec::flt ("time",     "Time",     5.0f, 2000.0f, 380.0f, "ms", 0.4f, 0.1f),
                ParamSpec::flt ("feedback", "Feedback", 0.0f, 0.95f, 0.45f, "%"),
                ParamSpec::flt ("tone",     "Tone",     500.0f, 18000.0f, 5500.0f, "Hz", 0.35f, 1.0f),
                ParamSpec::flt ("flutter",  "Flutter",  0.0f, 1.0f, 0.2f, "%"),
                ParamSpec::flt ("spread",   "Ping-pong",0.0f, 1.0f, 0.7f, "%"),
                ParamSpec::flt ("mix",      "Mix",      0.0f, 1.0f, 0.35f, "%"),
                ParamSpec::choice ("sync", "Sync", { "Free", "1/16", "1/8", "1/8 dotted", "1/4", "1/2" }, 0) };
            p.heroId = "time"; p.heroRect = { 0.05f, 0.05f, 0.50f, 0.40f };
            p.heroAxis = 0; p.heroSign = 1;
            p.glowPos = { 0.35f, 0.22f }; p.glowRadius = 0.30f;
            return p;
        }();
        return s;
    }

    class ButterflyDelay final : public MawProcessorBase
    {
    public:
        ButterflyDelay() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int, int) override
        {
            for (auto& d : dl) d.init ((int) (2.2 * sampleRate) + 4);
            for (auto& f : fbLp) f.reset();
            timeSm.reset (sampleRate, 0.15);
            timeSm.setCurrentAndTargetValue ((float) (0.38 * sampleRate));
            phase = 0.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const int nCh = juce::jmin (2, buffer.getNumChannels());

            float ms = getParam ("time");
            const int sync = getChoice ("sync");
            if (sync > 0)
            {
                static constexpr float beats[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f };
                ms = beats[sync] * 60000.0f / (float) getHostBpm();
            }
            timeSm.setTargetValue (juce::jlimit (5.0f, 2000.0f, ms) * 0.001f * (float) sr);

            const float fb = getParam ("feedback"), spread = getParam ("spread"), mix = getParam ("mix");
            const float a = maw::OnePoleLP::coeff (getParam ("tone"), sr);
            const float flutterSamples = getParam ("flutter") * 0.006f * (float) sr;
            const float inc = 0.4f / (float) sr;
            float peak = 0.0f;

            float* L = buffer.getWritePointer (0);
            float* R = nCh > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < n; ++i)
            {
                const float base = timeSm.getNextValue();
                const float dL = base + std::sin (juce::MathConstants<float>::twoPi * phase) * flutterSamples;
                const float dR = base + std::sin (juce::MathConstants<float>::twoPi * (phase + 0.25f)) * flutterSamples;
                phase += inc; if (phase >= 1.0f) phase -= 1.0f;

                const float inL = L[i];
                const float inR = R ? R[i] : inL;

                const float yL = dl[0].read (dL);
                const float yR = dl[1].read (dR);
                const float fbL = std::tanh (fbLp[0].process (yL, a) * fb);
                const float fbR = std::tanh (fbLp[1].process (yR, a) * fb);

                // spread = 1 is a true ping-pong: input enters left only, feedback crosses over
                dl[0].push (inL + (1.0f - spread) * fbL + spread * fbR);
                dl[1].push (inR * (1.0f - spread) + (1.0f - spread) * fbR + spread * fbL);

                peak = juce::jmax (peak, std::abs (yL), std::abs (yR));
                L[i] = inL * (1.0f - mix) + yL * mix;
                if (R) R[i] = inR * (1.0f - mix) + yR * mix;
            }
            reaction.store (juce::jlimit (0.0f, 1.0f, peak * 1.5f));
        }

    private:
        maw::DelayLine dl[2];
        maw::OnePoleLP fbLp[2];
        juce::SmoothedValue<float> timeSm;
        float phase = 0.0f;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ButterflyDelay(); }
