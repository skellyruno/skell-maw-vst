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
            p.fx = PluginSpec::fxEmbers;
            p.tongue = true;
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
            maxBlockSize = juce::jmax (1, maxBlock);
            oversampler.initProcessing ((size_t) maxBlockSize);
            oversampler.reset();
            mixer.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
            const float latency = oversampler.getLatencyInSamples();
            mixer.setWetLatency (latency);
            setLatencySamples (juce::roundToInt (latency));
            driveGain.reset (sampleRate * 4.0, 0.02);
            driveGain.setCurrentAndTargetValue (1.0f);
            lp[0].reset(); lp[1].reset();
            dc[0].reset(); dc[1].reset();
            dcCoeff = maw::DcBlock::coeff (8.0f, sampleRate * 4.0);
            lastOutGain = 1.0f;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            const int total = buffer.getNumSamples();
            const float drive = getParam ("drive"), tone = getParam ("tone");
            const int type = getChoice ("type");

            mixer.setWetMixProportion (getParam ("mix"));
            driveGain.setTargetValue (juce::Decibels::decibelsToGain (drive * 36.0f));
            const float a = maw::OnePoleLP::coeff (tone, sr * 4.0);

            // the oversampler only accepts blocks up to the size it was prepared for, so never hand it more
            juce::dsp::AudioBlock<float> whole (buffer);
            for (int start = 0; start < total; start += maxBlockSize)
                processChunk (whole.getSubBlock ((size_t) start, (size_t) juce::jmin (maxBlockSize, total - start)), type, a);

            // Output is the master level: it applies to the dry part too, so it still works at 0% mix
            const float outGain = juce::Decibels::decibelsToGain (getParam ("output"));
            buffer.applyGainRamp (0, total, lastOutGain, outGain);
            lastOutGain = outGain;
        }

    private:
        void processChunk (juce::dsp::AudioBlock<float> block, int type, float a)
        {
            mixer.pushDrySamples (block);
            auto up = oversampler.processSamplesUp (block);
            const size_t nCh = juce::jmin (up.getNumChannels(), (size_t) 2);

            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float g = driveGain.getNextValue();
                for (size_t c = 0; c < nCh; ++c)
                {
                    float* d = up.getChannelPointer (c);
                    // the Tube curve is asymmetric, so it creates DC; block it before it reaches the output
                    d[i] = dc[c].process (lp[c].process (shape (d[i] * g, type), a), dcCoeff);
                }
            }

            oversampler.processSamplesDown (block);
            mixer.mixWetSamples (block);
        }

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
        maw::DcBlock dc[2];
        float dcCoeff = 0.999f, lastOutGain = 1.0f;
        int maxBlockSize = 512;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SerpentsMaw(); }
