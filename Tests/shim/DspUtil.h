#pragma once
#include "juce_shim.h"
namespace maw
{
    struct OnePoleLP
    {
        float z = 0.0f;
        void reset() { z = 0.0f; }
        static float coeff (float fc, double sr)
        {
            return std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
        }
        float process (float x, float a) { z = (1.0f - a) * x + a * z; return z; }
    };

    // One-pole DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
    struct DcBlock
    {
        float x1 = 0.0f, y1 = 0.0f;
        void reset() { x1 = y1 = 0.0f; }
        static float coeff (float fc, double sr) { return 1.0f - juce::MathConstants<float>::twoPi * fc / (float) sr; }
        float process (float x, float r) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    };

    // RBJ biquad, transposed direct form II. No allocation when changing coefficients.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0.0f; }

        void setHighpass (double sr, float freq, float q = 0.7071f)
        {
            freq = juce::jlimit (10.0f, (float) (sr * 0.45), freq);
            const float w0 = juce::MathConstants<float>::twoPi * freq / (float) sr;
            const float c = std::cos (w0), s = std::sin (w0);
            const float alpha = s / (2.0f * q);
            const float a0 = 1.0f + alpha;
            b0 = ((1.0f + c) * 0.5f) / a0;
            b1 = -(1.0f + c) / a0;
            b2 = b0;
            a1 = (-2.0f * c) / a0;
            a2 = (1.0f - alpha) / a0;
        }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // Simple fractional delay line (linear interpolation). Delay must be >= 1 sample.
    struct DelayLine
    {
        std::vector<float> buf;
        int size = 0, w = 0;

        void init (int n) { size = juce::jmax (4, n); buf.assign ((size_t) size, 0.0f); w = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; if (++w >= size) w = 0; }
        float read (float delay) const
        {
            delay = juce::jlimit (1.0f, (float) (size - 2), delay);
            // integer and fractional parts are kept apart: a float read position loses the
            // fraction (and can round up to 'size') once the buffer is long
            const int di = (int) delay;
            const float f = delay - (float) di;
            int i0 = w - di;      if (i0 < 0) i0 += size;   // 'di' samples ago
            int i1 = i0 - 1;      if (i1 < 0) i1 += size;   // one sample older
            return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
        }
    };
}
