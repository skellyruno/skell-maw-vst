// Chapter 3 - The Lunge: Serpent's Maw (distortion)
#include "MawProcessorBase.h"
#include "DspUtil.h"

namespace
{
    const PluginSpec& makeSpec()
    {
        static const PluginSpec s = []
        {
            PluginSpec p;
            p.name = "Serpent's Maw"; p.chapter = "Chapter 3 - The Lunge"; p.tagline = "Feed the serpent.";
            p.accent = 0xffd8341f;
            p.crop = { 0.40f, 0.22f, 0.60f, 0.60f };
            p.params = {
                ParamSpec::flt ("drive",  "Drive",  0.0f, 1.0f, 0.3f, "%"),
                ParamSpec::flt ("tone",   "Tone",   200.0f, 18000.0f, 8000.0f, "Hz", 0.3f, 1.0f),
                ParamSpec::flt ("mix",    "Mix",    0.0f, 1.0f, 1.0f, "%"),
                ParamSpec::flt ("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1.0f, 0.1f),
                ParamSpec::choice ("type", "Bite", { "Soft", "Tube", "Fold", "Hard" }, 0) };
            p.heroId = "drive"; p.heroRect = { 0.49f, 0.60f, 0.28f, 0.18f };
            p.heroAxis = 0; p.heroSign = -1;       // pull the tongue left to feed the snake
            p.glowPos = { 0.72f, 0.44f }; p.glowRadius = 0.20f;
            return p;
        }();
        return s;
    }

    class SerpentsMaw final : public MawProcessorBase
    {
    public:
        SerpentsMaw() : MawProcessorBase (makeSpec()) {}

    protected:
        void prepare (double sampleRate, int maxBlock, int) override
        {
            oversampler.initProcessing ((size_t) maxBlock);
            oversampler.reset();
            mixer.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
            const float latency = oversampler.getLatencyInSamples();
            mixer.setWetLatency (latency);
            setLatencySamples (juce::roundToInt (latency));
            driveGain.reset (sampleRate * 4.0, 0.02);
            driveGain.setCurrentAndTargetValue (1.0f);
            lp[0].reset(); lp[1].reset();
            lastOutGain = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int n = buffer.getNumSamples();
            const float drive = getParam ("drive"), tone = getParam ("tone"), mix = getParam ("mix");
            const int type = getChoice ("type");

            juce::dsp::AudioBlock<float> block (buffer);
            mixer.setWetMixProportion (mix);
            mixer.pushDrySamples (block);

            auto up = oversampler.processSamplesUp (block);
            driveGain.setTargetValue (juce::Decibels::decibelsToGain (drive * 36.0f));
            const float a = maw::OnePoleLP::coeff (tone, sr * 4.0);
            const size_t nCh = juce::jmin (up.getNumChannels(), (size_t) 2);

            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float g = driveGain.getNextValue();
                for (size_t c = 0; c < nCh; ++c)
                {
                    float* d = up.getChannelPointer (c);
                    d[i] = lp[c].process (shape (d[i] * g, type), a);
                }
            }
            oversampler.processSamplesDown (block);

            const float outGain = juce::Decibels::decibelsToGain (getParam ("output"));
            buffer.applyGainRamp (0, n, lastOutGain, outGain);
            lastOutGain = outGain;
            mixer.mixWetSamples (block);
        }

    private:
        static float shape (float x, int type) noexcept
        {
            switch (type)
            {
                case 0:  return std::tanh (x);
                case 1:  return std::tanh (x + 0.3f) - std::tanh (0.3f);
                case 2:  return std::sin (x);
                default: return juce::jlimit (-1.0f, 1.0f, x);
            }
        }

        juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
        juce::dsp::DryWetMixer<float> mixer;
        juce::SmoothedValue<float> driveGain;
        maw::OnePoleLP lp[2];
        float lastOutGain = 1.0f;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SerpentsMaw(); }
