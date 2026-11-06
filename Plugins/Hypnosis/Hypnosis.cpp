// Chapter 2 - The Gaze: Hypnosis (phaser)
#include "MawProcessorBase.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Hypnosis"; p.chapter = "Chapter 2 - The Gaze"; p.tagline = "Look into the eye.";
            p.accent = 0xffe08a2d;
            p.crop = { 0.66f, 0.17f, 0.34f, 0.34f };
            p.params = {
                ParamSpec::flt ("rate",     "Rate",     0.05f, 12.0f, 0.6f, "Hz", 0.4f, 0.01f),
                ParamSpec::flt ("depth",    "Depth",    0.0f, 1.0f, 0.7f, "%"),
                ParamSpec::flt ("centre",   "Centre",   100.0f, 8000.0f, 1200.0f, "Hz", 0.3f, 1.0f),
                ParamSpec::flt ("feedback", "Feedback", -0.9f, 0.9f, 0.4f, "%"),
                ParamSpec::flt ("mix",      "Mix",      0.0f, 1.0f, 0.5f, "%") };
            p.heroId = "rate"; p.heroRect = { 0.80f, 0.26f, 0.17f, 0.17f };
            p.heroAxis = 1; p.heroSign = 1;
            p.glowPos = { 0.868f, 0.345f }; p.glowRadius = 0.10f;
            return p;
        }();
        return s;
    }

    class Hypnosis final : public MawProcessorBase
    {
    public:
        Hypnosis() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int maxBlock, int numChannels) override
        {
            phaser.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) juce::jmax (1, numChannels) });
            phaser.reset();
            phase = 0.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const float rate = getParam ("rate");
            phaser.setRate (rate);
            phaser.setDepth (getParam ("depth"));
            phaser.setCentreFrequency (getParam ("centre"));
            phaser.setFeedback (getParam ("feedback"));
            phaser.setMix (getParam ("mix"));

            juce::dsp::AudioBlock<float> block (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            phaser.process (ctx);

            // the eye pulses with the LFO
            phase += rate * (float) buffer.getNumSamples() / (float) sr;
            phase -= std::floor (phase);
            reaction.store (0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * phase));
        }

    private:
        juce::dsp::Phaser<float> phaser;
        float phase = 0.0f;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Hypnosis(); }
